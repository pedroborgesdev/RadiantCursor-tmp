import { createHash } from "node:crypto";
import { constants } from "node:fs";
import { access, mkdir, open, readFile, readdir, rename, rm, stat, writeFile } from "node:fs/promises";
import { basename, dirname, join } from "node:path";

import { MOTION_BUILTINS } from "../../shared/motion/builtins";
import { compileEffect } from "../../shared/motion/compiler";
import { migrateLegacyDocument } from "../../shared/motion/migrate";
import { ENGINE_CAPABILITIES, ENGINE_VERSION } from "../../shared/engine/limits";
import type { DeployResult, EffectDocumentV2, EffectSummary, ImportResult, RuntimeDefinition, SaveResult } from "../../shared/motion/schema";
import type { RuntimeStatus } from "../../shared/engine/schema";
import { assertMotionDocument, validateMotionDocument } from "../../shared/motion/validator";
import { AssetStore } from "./asset-store";
import { createZip, readZip } from "./zip";

const SAFE_ID = /^[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}$/;

interface CurrentRevisionRecord {
  revision: string;
  deployedAt: string;
}

async function exists(path: string): Promise<boolean> {
  try { await access(path, constants.F_OK); return true; } catch { return false; }
}

function assertSafeId(id: unknown): asserts id is string {
  if (typeof id !== "string" || !SAFE_ID.test(id) || basename(id) !== id) {
    throw new Error("ID de efeito inválido.");
  }
}

function stableJson(value: unknown): string {
  if (Array.isArray(value)) return `[${value.map(stableJson).join(",")}]`;
  if (value && typeof value === "object") {
    return `{${Object.entries(value).sort(([a], [b]) => a.localeCompare(b)).map(([key, entry]) => `${JSON.stringify(key)}:${stableJson(entry)}`).join(",")}}`;
  }
  return JSON.stringify(value);
}

function revisionFor(runtime: RuntimeDefinition): `sha256:${string}` {
  const { revision: _revision, ...canonical } = runtime;
  return `sha256:${createHash("sha256").update(stableJson(canonical)).digest("hex")}`;
}

async function atomicWrite(path: string, contents: string | Buffer): Promise<void> {
  await mkdir(dirname(path), { recursive: true, mode: 0o700 });
  const temporary = `${path}.tmp-${process.pid}-${Date.now()}`;
  const handle = await open(temporary, "wx", 0o600);
  try {
    if (typeof contents === "string") await handle.writeFile(contents, "utf8");
    else await handle.writeFile(contents);
    await handle.sync();
  } finally {
    await handle.close();
  }
  await rename(temporary, path);
}

async function readJson(path: string): Promise<unknown> {
  const fileStat = await stat(path);
  if (!fileStat.isFile() || fileStat.size > 256 * 1024) throw new Error("Arquivo de efeito inválido ou grande demais.");
  return JSON.parse(await readFile(path, "utf8"));
}

export class EffectRepository {
  readonly root: string;
  readonly effectsRoot: string;
  readonly assetsRoot: string;
  readonly assetStore: AssetStore;

  constructor(dataRoot: string, exactRoot = false) {
    this.root = exactRoot ? dataRoot : join(dataRoot, "radiantcursor-studio");
    this.effectsRoot = join(this.root, "library", "effects");
    this.assetsRoot = join(this.root, "assets", "sha256");
    this.assetStore = new AssetStore(this.assetsRoot);
  }

  async initialize(): Promise<void> {
    await Promise.all([
      mkdir(this.effectsRoot, { recursive: true, mode: 0o700 }),
      mkdir(this.assetsRoot, { recursive: true, mode: 0o700 }),
      mkdir(join(this.root, "cache", "thumbnails"), { recursive: true, mode: 0o700 }),
    ]);
    for (const builtIn of MOTION_BUILTINS) {
      const metadataPath = join(this.effectRoot(builtIn.id), "metadata.json");
      if (!(await exists(metadataPath))) await this.saveDraft(builtIn, true);
      else {
        try { const raw = await readJson(join(this.effectRoot(builtIn.id), "draft.json")); if ((raw as { schemaVersion?: unknown }).schemaVersion !== 2) await this.saveDraft(builtIn, true); } catch { await this.saveDraft(builtIn, true); }
      }
    }
  }

  private effectRoot(id: string): string {
    assertSafeId(id);
    return join(this.effectsRoot, id);
  }

  async listEffects(): Promise<EffectSummary[]> {
    await this.initialize();
    const entries = await readdir(this.effectsRoot, { withFileTypes: true });
    const summaries = await Promise.all(entries.filter((entry) => entry.isDirectory() && SAFE_ID.test(entry.name)).map(async (entry): Promise<EffectSummary | null> => {
      try {
        const effectRoot = this.effectRoot(entry.name);
        const document = await this.loadEffect(entry.name);
        const current = await this.readCurrent(effectRoot);
        const fileStat = await stat(join(effectRoot, "draft.json"));
        return {
          id: document.id,
          name: document.metadata.name,
          description: document.metadata.description,
          revision: current?.revision ?? null,
          updatedAt: fileStat.mtime.toISOString(),
          layerCount: Object.keys(document.elements).length,
          favorite: document.metadata.favorite ?? false,
          builtIn: document.metadata.tags.includes("built-in"),
        };
      } catch { return null; }
    }));
    return summaries.filter((summary): summary is EffectSummary => summary !== null).sort((a, b) => Number(b.favorite) - Number(a.favorite) || a.name.localeCompare(b.name, "pt-BR"));
  }

  async loadEffect(id: unknown): Promise<EffectDocumentV2> {
    assertSafeId(id);
    const raw = await readJson(join(this.effectRoot(id), "draft.json"));
    const migrated = migrateLegacyDocument(raw);
    if (migrated) { await this.saveDraft(migrated); return migrated; }
    return assertMotionDocument(raw);
  }

  async saveDraft(input: unknown, builtIn = false): Promise<SaveResult> {
    const document = assertMotionDocument(input);
    const root = this.effectRoot(document.id);
    const savedAt = new Date().toISOString();
    await mkdir(root, { recursive: true, mode: 0o700 });
    await atomicWrite(join(root, "draft.json"), `${JSON.stringify(document, null, 2)}\n`);
    await atomicWrite(join(root, "metadata.json"), `${JSON.stringify({ ...document.metadata, builtIn, savedAt }, null, 2)}\n`);
    return { document, savedAt };
  }

  async deployEffect(input: unknown): Promise<DeployResult> {
    const result = validateMotionDocument(input);
    if (!result.valid || !result.document) return Promise.reject(Object.assign(new Error("O efeito contém erros e não pode ser aplicado."), { diagnostics: result.diagnostics }));
    const compiled = compileEffect(result.document);
    const revision = revisionFor(compiled);
    const document: EffectDocumentV2 = { ...result.document, revision };
    const runtime: RuntimeDefinition = { ...compiled, revision };
    const root = this.effectRoot(document.id);
    const revisionName = revision.slice("sha256:".length);
    const revisionRoot = join(root, "revisions", revisionName);
    await mkdir(revisionRoot, { recursive: true, mode: 0o700 });
    await atomicWrite(join(revisionRoot, "document.json"), `${JSON.stringify(document, null, 2)}\n`);
    await atomicWrite(join(revisionRoot, "runtime.json"), `${JSON.stringify(runtime)}\n`);
    await atomicWrite(join(revisionRoot, "manifest.json"), `${JSON.stringify({ schemaVersion: 2, runtimeVersion: 1, effectId: document.id, revision, engineVersion: ENGINE_VERSION, capabilities: ["motion.shapes.v2", "motion.hierarchy.v1", "motion.tracks.v1"] }, null, 2)}\n`);
    await this.saveDraft(document, document.metadata.tags.includes("built-in"));
    const deployedAt = new Date().toISOString();
    await atomicWrite(join(root, "current.json"), `${JSON.stringify({ revision, deployedAt } satisfies CurrentRevisionRecord, null, 2)}\n`);
    return { effectId: document.id, revision, deployedAt, diagnostics: result.diagnostics };
  }

  private async readCurrent(root: string): Promise<CurrentRevisionRecord | null> {
    try {
      const raw = await readJson(join(root, "current.json"));
      if (typeof raw === "object" && raw !== null && "revision" in raw && typeof raw.revision === "string") {
        return raw as CurrentRevisionRecord;
      }
    } catch { /* first deploy */ }
    return null;
  }

  async deleteEffect(id: unknown): Promise<void> {
    assertSafeId(id);
    if (MOTION_BUILTINS.some((effect) => effect.id === id)) throw new Error("Efeitos nativos não podem ser removidos.");
    await rm(this.effectRoot(id), { recursive: true, force: false, maxRetries: 2 });
  }

  async getRuntimeStatus(activeEffectId: string | null, activeRevision: string | null): Promise<RuntimeStatus> {
    return {
      engineVersion: ENGINE_VERSION,
      activeEffectId,
      activeRevision,
      lastKnownGoodRevision: activeRevision,
      diagnostics: [],
      capabilities: ENGINE_CAPABILITIES,
    };
  }

  async exportEffect(id: unknown): Promise<Buffer> {
    const document = await this.loadEffect(id);
    const entries = new Map<string, Buffer>();
    entries.set("effect.json", Buffer.from(`${JSON.stringify(document, null, 2)}\n`));
    entries.set("metadata.json", Buffer.from(`${JSON.stringify(document.metadata, null, 2)}\n`));
    entries.set("manifest.json", Buffer.from(`${JSON.stringify({ formatVersion: 2, effectId: document.id, schemaVersion: 2, assets: [] }, null, 2)}\n`));
    return createZip(entries);
  }

  async importEffect(archive: Buffer, sourcePath: string): Promise<ImportResult> {
    const entries = readZip(archive);
    const effectBytes = entries.get("effect.json");
    const manifestBytes = entries.get("manifest.json");
    if (!effectBytes || !manifestBytes || effectBytes.length > 256 * 1024 || manifestBytes.length > 64 * 1024) throw new Error("Bundle sem manifest.json/effect.json válido.");
    const raw = JSON.parse(effectBytes.toString("utf8"));
    const document = assertMotionDocument(migrateLegacyDocument(raw) ?? raw);
    const manifest = JSON.parse(manifestBytes.toString("utf8")) as { formatVersion?: unknown; effectId?: unknown; assets?: unknown };
    if ((manifest.formatVersion !== 1 && manifest.formatVersion !== 2) || manifest.effectId !== document.id || !Array.isArray(manifest.assets)) throw new Error("Manifesto do bundle incompatível.");
    let importedAssets = 0;
    for (const entry of manifest.assets) {
      if (!entry || typeof entry !== "object" || !("hash" in entry) || !("path" in entry) || typeof entry.hash !== "string" || typeof entry.path !== "string" || !/^sha256:[a-f0-9]{64}$/.test(entry.hash) || !/^assets\/[a-f0-9]{64}\.(png|jpg|webp)$/.test(entry.path)) throw new Error("Registro de asset inválido no bundle.");
      const data = entries.get(entry.path);
      if (!data || createHash("sha256").update(data).digest("hex") !== entry.hash.slice(7)) throw new Error("Hash de asset inválido no bundle.");
      const destination = join(this.assetsRoot, entry.path.slice("assets/".length));
      if (!(await exists(destination))) await atomicWrite(destination, data);
      importedAssets += 1;
    }
    await this.saveDraft(document);
    return { document, importedAssets, sourcePath };
  }
}
