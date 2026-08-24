import assert from "node:assert/strict";
import { mkdtemp, readFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

import { WindowsRuntimeAdapter } from "../src/main/platform/windows/windows-adapter";
import { DEFAULT_RADIANT_CURSOR_SETTINGS } from "../src/shared/types";

test("adaptador Windows persiste configuração validada sem iniciar o runtime", async () => {
  const root = await mkdtemp(join(tmpdir(), "radiantcursor-windows-adapter-"));
  const loginChanges: boolean[] = [];
  const adapter = new WindowsRuntimeAdapter({
    dataRoot: root,
    resourcesPath: join(root, "resources"),
    appPath: root,
    packaged: false,
    setLoginItem: (enabled) => loginChanges.push(enabled),
  });

  const state = await adapter.applySettings({ ...DEFAULT_RADIANT_CURSOR_SETTINGS, Style: "nova", TrailOffsetX: -4, TrailOffsetY: 12, TrailDistance: 18 });
  assert.equal(state.settings.Style, "nova");
  assert.equal(state.isLoaded, false);
  assert.equal(state.compatibility.platform, "windows");
  assert.equal(state.compatibility.runtimeInstalled, false);
  assert.deepEqual(loginChanges, []);

  const stored = JSON.parse(await readFile(join(root, "runtime", "state.json"), "utf8")) as { settings: { Style: string; TrailOffsetX: number; TrailOffsetY: number; TrailDistance: number }; enabled: boolean };
  assert.equal(stored.settings.Style, "nova");
  assert.equal(stored.settings.TrailOffsetX, -4);
  assert.equal(stored.settings.TrailOffsetY, 12);
  assert.equal(stored.settings.TrailDistance, 18);
  assert.equal(stored.enabled, false);
});

test("falha ao iniciar runtime ausente reverte a ativação", async () => {
  const root = await mkdtemp(join(tmpdir(), "radiantcursor-windows-rollback-"));
  const adapter = new WindowsRuntimeAdapter({
    dataRoot: root,
    resourcesPath: join(root, "resources"),
    appPath: root,
    packaged: false,
    setLoginItem: () => undefined,
  });

  await assert.rejects(() => adapter.activateEffect({ ...DEFAULT_RADIANT_CURSOR_SETTINGS }));
  const stored = JSON.parse(await readFile(join(root, "runtime", "state.json"), "utf8")) as { enabled: boolean };
  assert.equal(stored.enabled, false);
});
