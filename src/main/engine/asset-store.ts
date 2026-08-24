import { createHash } from "node:crypto";
import { constants } from "node:fs";
import { access, mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { join } from "node:path";

import { ENGINE_LIMITS } from "../../shared/engine/limits";
import type { AssetRecord } from "../../shared/engine/schema";

const EXTENSIONS = { "image/png": "png", "image/jpeg": "jpg", "image/webp": "webp" } as const;

async function exists(path: string) { try { await access(path, constants.F_OK); return true; } catch { return false; } }

export class AssetStore {
  constructor(readonly root: string) {}

  async importImage(data: Buffer, mediaType: keyof typeof EXTENSIONS, width: number, height: number): Promise<AssetRecord> {
    if (data.length < 16 || data.length > 32 * 1024 * 1024) throw new Error("Imagem vazia ou maior que 32 MiB.");
    if (width < 1 || height < 1 || width > ENGINE_LIMITS.maxTextureDimension || height > ENGINE_LIMITS.maxTextureDimension) throw new Error(`A imagem deve ter no máximo ${ENGINE_LIMITS.maxTextureDimension}×${ENGINE_LIMITS.maxTextureDimension}.`);
    const hash = createHash("sha256").update(data).digest("hex");
    const destination = join(this.root, `${hash}.${EXTENSIONS[mediaType]}`);
    await mkdir(this.root, { recursive: true, mode: 0o700 });
    if (!(await exists(destination))) {
      const temporary = `${destination}.tmp-${process.pid}`;
      await writeFile(temporary, data, { mode: 0o600, flag: "wx" });
      await rename(temporary, destination);
    }
    return { assetId: `sha256:${hash}`, mediaType, width, height, bytes: data.length };
  }

  async readAsset(hash: string): Promise<{ data: Buffer; extension: string } | null> {
    if (!/^[a-f0-9]{64}$/.test(hash)) return null;
    for (const extension of Object.values(EXTENSIONS)) {
      try { return { data: await readFile(join(this.root, `${hash}.${extension}`)), extension }; } catch { /* try next */ }
    }
    return null;
  }
}
