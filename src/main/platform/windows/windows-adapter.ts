import { constants } from "node:fs";
import { access, mkdir, open, readFile, rename } from "node:fs/promises";
import { createConnection } from "node:net";
import { basename, dirname, isAbsolute, join, resolve } from "node:path";
import { spawn } from "node:child_process";

import {
  DEFAULT_RADIANT_CURSOR_SETTINGS,
  type RadiantCursorSettings,
  type RadiantCursorState,
  type RuntimeCompatibility,
} from "../../../shared/types";
import { validateRadiantCursorSettings } from "../kwin/kwin-adapter";
import type { ActiveEngineRevision, RuntimeAdapter, RuntimeStateFile } from "../runtime-adapter";

const PIPE_PATH = String.raw`\\.\pipe\LOCAL\RadiantCursor.Runtime`;
const SAFE_EFFECT_ID = /^[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}$/;
const SAFE_REVISION = /^sha256:[a-f0-9]{64}$/;

export class WindowsIntegrationError extends Error {
  constructor(message: string, options?: ErrorOptions) {
    super(message, options);
    this.name = "WindowsIntegrationError";
  }
}

async function exists(path: string): Promise<boolean> {
  try { await access(path, constants.F_OK); return true; } catch { return false; }
}

async function atomicWrite(path: string, value: unknown): Promise<void> {
  await mkdir(dirname(path), { recursive: true, mode: 0o700 });
  const temporary = `${path}.tmp-${process.pid}-${Date.now()}`;
  const handle = await open(temporary, "wx", 0o600);
  try {
    await handle.writeFile(`${JSON.stringify(value, null, 2)}\n`, "utf8");
    await handle.sync();
  } finally {
    await handle.close();
  }
  await rename(temporary, path);
}

function sendCommand(command: string, timeoutMs = 1_200): Promise<string> {
  return new Promise((resolveReply, reject) => {
    let settled = false;
    let reply = "";
    const socket = createConnection(PIPE_PATH);
    const timer = setTimeout(() => finish(new Error("Tempo esgotado ao acessar o runtime.")), timeoutMs);
    const finish = (error?: Error) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      socket.destroy();
      if (error) reject(error); else resolveReply(reply.trim());
    };
    socket.setEncoding("utf8");
    socket.once("connect", () => socket.write(`${command}\n`));
    socket.on("data", (chunk: string) => {
      reply += chunk;
      if (reply.includes("\n")) finish();
    });
    socket.once("error", (error) => finish(error));
    socket.once("end", () => finish());
  });
}

export interface WindowsAdapterOptions {
  dataRoot: string;
  resourcesPath: string;
  appPath: string;
  packaged: boolean;
  setLoginItem(enabled: boolean, executable: string, runtimeArguments: string[]): void;
}

export class WindowsRuntimeAdapter implements RuntimeAdapter {
  private readonly statePath: string;
  private mutationTail: Promise<void> = Promise.resolve();

  constructor(private readonly options: WindowsAdapterOptions) {
    this.statePath = join(options.dataRoot, "runtime", "state.json");
  }

  private enqueue<T>(operation: () => Promise<T>): Promise<T> {
    const pending = this.mutationTail.then(operation, operation);
    this.mutationTail = pending.then(() => undefined, () => undefined);
    return pending;
  }

  private runtimeCandidates(): string[] {
    const override = process.env.RADIANTCURSOR_WINDOWS_RUNTIME;
    return [
      ...(override && isAbsolute(override) ? [resolve(override)] : []),
      join(this.options.resourcesPath, "runtime", "windows", "RadiantCursor.Runtime.exe"),
      ...(process.env.LOCALAPPDATA ? [join(process.env.LOCALAPPDATA, "Programs", "RadiantCursor", "resources", "runtime", "windows", "RadiantCursor.Runtime.exe")] : []),
      join(this.options.appPath, "native", "windows", "build", "Release", "RadiantCursor.Runtime.exe"),
      join(this.options.appPath, "native", "windows", "build", "RadiantCursor.Runtime.exe"),
    ];
  }

  private async runtimePath(): Promise<string | null> {
    for (const candidate of this.runtimeCandidates()) if (await exists(candidate)) return candidate;
    return null;
  }

  private async readStateFile(): Promise<RuntimeStateFile> {
    try {
      const raw = JSON.parse(await readFile(this.statePath, "utf8")) as Partial<RuntimeStateFile>;
      return {
        schemaVersion: 1,
        enabled: raw.enabled === true,
        settings: validateRadiantCursorSettings({
          ...DEFAULT_RADIANT_CURSOR_SETTINGS,
          ...(raw.settings && typeof raw.settings === "object" ? raw.settings : {}),
        }),
        activeEffectId: typeof raw.activeEffectId === "string" && SAFE_EFFECT_ID.test(raw.activeEffectId) ? raw.activeEffectId : null,
        activeRevision: typeof raw.activeRevision === "string" && SAFE_REVISION.test(raw.activeRevision) ? raw.activeRevision : null,
        activeHaloEffectId: typeof raw.activeHaloEffectId === "string" && SAFE_EFFECT_ID.test(raw.activeHaloEffectId) ? raw.activeHaloEffectId : null,
        activeHaloRevision: typeof raw.activeHaloRevision === "string" && SAFE_REVISION.test(raw.activeHaloRevision) ? raw.activeHaloRevision : null,
        updatedAt: typeof raw.updatedAt === "string" ? raw.updatedAt : new Date(0).toISOString(),
      };
    } catch {
      return {
        schemaVersion: 1,
        enabled: false,
        settings: { ...DEFAULT_RADIANT_CURSOR_SETTINGS },
        activeEffectId: null,
        activeRevision: null,
        activeHaloEffectId: null,
        activeHaloRevision: null,
        updatedAt: new Date().toISOString(),
      };
    }
  }

  private async writeStateFile(state: RuntimeStateFile): Promise<void> {
    await atomicWrite(this.statePath, { ...state, updatedAt: new Date().toISOString() });
  }

  private async isRunning(): Promise<boolean> {
    try { return (await sendCommand("PING", 450)).startsWith("OK"); } catch { return false; }
  }

  private async ensureRunning(): Promise<void> {
    if (await this.isRunning()) return;
    const executable = await this.runtimePath();
    if (!executable) {
      throw new WindowsIntegrationError("RadiantCursor.Runtime.exe não foi encontrado. Reinstale o RadiantCursor para Windows.");
    }
    const child = spawn(executable, ["--data-dir", this.options.dataRoot], {
      detached: true,
      stdio: "ignore",
      windowsHide: true,
    });
    child.unref();
    for (let attempt = 0; attempt < 30; attempt += 1) {
      await new Promise((resolveWait) => setTimeout(resolveWait, 100));
      if (await this.isRunning()) return;
    }
    throw new WindowsIntegrationError("O runtime do Windows foi iniciado, mas não respondeu.");
  }

  private async reload(startIfNeeded: boolean): Promise<void> {
    if (startIfNeeded) await this.ensureRunning();
    else if (!await this.isRunning()) return;
    const reply = await sendCommand("RELOAD");
    if (!reply.startsWith("OK")) throw new WindowsIntegrationError(reply || "O runtime recusou a configuração.");
  }

  private async setAutoStart(enabled: boolean): Promise<void> {
    const executable = await this.runtimePath();
    if (executable) this.options.setLoginItem(enabled, executable, ["--data-dir", this.options.dataRoot]);
  }

  private async commitState(
    previous: RuntimeStateFile,
    next: RuntimeStateFile,
    startRuntime: boolean,
    autoStart: boolean | null,
  ): Promise<void> {
    await this.writeStateFile(next);
    try {
      await this.reload(startRuntime);
      if (autoStart !== null) await this.setAutoStart(autoStart);
    } catch (error) {
      await this.writeStateFile(previous);
      try { await this.reload(false); } catch { /* the original error is more useful */ }
      throw error;
    }
  }

  async checkCompatibility(): Promise<RuntimeCompatibility> {
    const runtime = await this.runtimePath();
    const running = await this.isRunning();
    const details: string[] = [];
    if (!runtime) details.push("RadiantCursor.Runtime.exe não foi localizado no pacote.");
    if (runtime && !running) details.push("O runtime está instalado e será iniciado ao ativar um efeito.");
    return {
      platform: "windows",
      compatible: runtime !== null,
      runtimeInstalled: runtime !== null,
      transportAvailable: running,
      details,
    };
  }

  async getState(): Promise<RadiantCursorState> {
    const [state, compatibility] = await Promise.all([this.readStateFile(), this.checkCompatibility()]);
    return { settings: state.settings, isLoaded: state.enabled && compatibility.transportAvailable, compatibility, backupPath: null };
  }

  applySettings(value: unknown): Promise<RadiantCursorState> {
    const settings = validateRadiantCursorSettings(value);
    return this.enqueue(async () => {
      const current = await this.readStateFile();
      await this.commitState(current, { ...current, settings, activeEffectId: null, activeRevision: null, activeHaloEffectId: null, activeHaloRevision: null }, false, null);
      return this.getState();
    });
  }

  activateEffect(value?: unknown): Promise<RadiantCursorState> {
    const settings = value === undefined ? undefined : validateRadiantCursorSettings(value);
    return this.enqueue(async () => {
      const current = await this.readStateFile();
      await this.commitState(current, {
        ...current,
        enabled: true,
        settings: settings ?? current.settings,
        activeEffectId: settings ? null : current.activeEffectId,
        activeRevision: settings ? null : current.activeRevision,
        activeHaloEffectId: settings ? null : current.activeHaloEffectId,
        activeHaloRevision: settings ? null : current.activeHaloRevision,
      }, true, true);
      return this.getState();
    });
  }

  disableEffect(): Promise<RadiantCursorState> {
    return this.enqueue(async () => {
      const current = await this.readStateFile();
      await this.commitState(current, { ...current, enabled: false }, false, false);
      return this.getState();
    });
  }

  async readActiveEngineRevision(target: "click" | "halo" = "click"): Promise<ActiveEngineRevision> {
    const state = await this.readStateFile();
    return target === "halo"
      ? { effectId: state.activeHaloEffectId, revision: state.activeHaloRevision }
      : { effectId: state.activeEffectId, revision: state.activeRevision };
  }

  activateEngineRevision(effectId: string, revision: string, target: "click" | "halo" = "click"): Promise<void> {
    if (!SAFE_EFFECT_ID.test(effectId) || !SAFE_REVISION.test(revision) || basename(effectId) !== effectId) {
      throw new WindowsIntegrationError("Efeito ou revisão declarativa inválida.");
    }
    return this.enqueue(async () => {
      const current = await this.readStateFile();
      const next = target === "halo"
        ? { ...current, enabled: true, activeHaloEffectId: effectId, activeHaloRevision: revision }
        : { ...current, enabled: true, activeEffectId: effectId, activeRevision: revision };
      await this.commitState(current, next, true, true);
    });
  }
}
