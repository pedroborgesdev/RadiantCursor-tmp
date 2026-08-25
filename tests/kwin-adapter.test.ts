import assert from "node:assert/strict";
import test from "node:test";

import { buildWriteKeyArguments, validateRadiantCursorSettings } from "../src/main/platform/kwin/kwin-adapter";
import { DEFAULT_RADIANT_CURSOR_SETTINGS } from "../src/shared/types";

test("separa valores negativos das opções do kwriteconfig", () => {
  assert.deepEqual(
    buildWriteKeyArguments("Effect-radiantcursor", "CursorLinkOffsetX", "-3"),
    [
      "--file",
      "kwinrc",
      "--group",
      "Effect-radiantcursor",
      "--key",
      "CursorLinkOffsetX",
      "--",
      "-3",
    ],
  );
});

test("aceita velocidade zero para parar somente o giro do halo", () => {
  const settings = validateRadiantCursorSettings({
    ...DEFAULT_RADIANT_CURSOR_SETTINGS,
    HaloSpeed: 0,
  });

  assert.equal(settings.HaloSpeed, 0);
  assert.throws(() => validateRadiantCursorSettings({
    ...DEFAULT_RADIANT_CURSOR_SETTINGS,
    HaloSpeed: -0.1,
  }));
});
