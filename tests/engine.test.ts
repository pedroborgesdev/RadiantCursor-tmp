import assert from "node:assert/strict";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

import { EffectRepository } from "../src/main/engine/effect-repository";
import { createZip, readZip } from "../src/main/engine/zip";
import { createAnimation, createBlankMotionEffect, createShape, MOTION_BUILTINS } from "../src/shared/motion/builtins";
import { compileEffect } from "../src/shared/motion/compiler";
import { flattenElements, scaleGroupTiming } from "../src/shared/motion/hierarchy";
import type { GroupElementDocument } from "../src/shared/motion/schema";
import { DEFAULT_TRANSFORM } from "../src/shared/motion/schema";
import { MOTION_LIMITS, validateMotionDocument } from "../src/shared/motion/validator";

test("composições nativas respeitam o schema motion v2", () => {
  assert.ok(MOTION_BUILTINS.length >= 2);
  for (const effect of MOTION_BUILTINS) {
    const result = validateMotionDocument(effect);
    assert.equal(result.valid, true, `${effect.id}: ${JSON.stringify(result.diagnostics)}`);
    const runtime = compileEffect(effect);
    assert.equal(runtime.nodes.length, Object.keys(effect.elements).length);
    assert.equal(runtime.effectId, effect.id);
  }
});

test("projetos de halo compilam e publicam com destino independente", async () => {
  const effect = createBlankMotionEffect("Halo de teste", "halo"); effect.id = "halo-fixture";
  const runtime = compileEffect(effect);
  assert.equal(runtime.target, "halo");
  assert.ok(runtime.nodes.length > 0);
  const temporary = await mkdtemp(join(tmpdir(), "radiantcursor-halo-"));
  try {
    const repository = new EffectRepository(temporary);
    const deployed = await repository.deployEffect(effect);
    assert.equal(deployed.target, "halo");
    assert.equal((await repository.loadEffect(effect.id)).target, "halo");
  } finally { await rm(temporary, { recursive: true, force: true }); }
});

test("hierarquia é normalizada, detecta ciclos e respeita o limite", () => {
  const effect = createBlankMotionEffect();
  const group: GroupElementDocument = { id: "group", type: "group", name: "Grupo", visible: true, locked: false, timing: { startMs: 0, durationMs: 500 }, transform: { ...DEFAULT_TRANSFORM, size: [1, 1] }, opacity: 1, animations: [], children: [effect.rootIds[0]!] };
  effect.elements[group.id] = group; effect.rootIds = [group.id];
  assert.deepEqual(flattenElements(effect).map((x) => x.depth), [0, 1]);
  group.children.push(group.id);
  assert.ok(validateMotionDocument(effect).diagnostics.some((entry) => entry.code === "cycle"));
  group.children.pop();
  for (let index = 0; index < MOTION_LIMITS.maxElements; index++) { const shape = createShape("circle"); shape.id = `extra-${index}`; effect.elements[shape.id] = shape; effect.rootIds.push(shape.id); }
  assert.ok(validateMotionDocument(effect).diagnostics.some((entry) => entry.code === "element_limit"));
});

test("timing de grupo estica filhos e animações proporcionalmente", () => {
  const effect = structuredClone(MOTION_BUILTINS[0]!); const group = effect.elements[effect.rootIds[0]!] as GroupElementDocument; const child = effect.elements[group.children[0]!]!; child.timing.startMs = 100; child.animations[0]!.timing.startMs = 50;
  const previous = group.timing.durationMs; scaleGroupTiming(effect, group.id, previous * 2);
  assert.equal(child.timing.startMs, 200); assert.equal(child.animations[0]!.timing.startMs, 100);
});

test("presets são compilados em tracks genéricas combináveis", () => {
  const effect = createBlankMotionEffect(); const element = effect.elements[effect.rootIds[0]!]!; element.animations = [createAnimation("moveAway"), createAnimation("spin"), createAnimation("fadeOut")];
  const runtime = compileEffect(effect), node = runtime.nodes[0]!;
  assert.ok(node.channels.some((track) => track.property === "position.x"));
  assert.ok(node.channels.some((track) => track.property === "rotation"));
  assert.ok(node.channels.some((track) => track.property === "opacity" && track.composition === "multiply"));
});

test("repositório publica document/runtime imutáveis e recarrega o draft", async () => {
  const temporary = await mkdtemp(join(tmpdir(), "radiantcursor-test-"));
  try {
    const repository = new EffectRepository(temporary); await repository.initialize(); const effect = createBlankMotionEffect(); effect.id = "repository-fixture";
    await repository.saveDraft(effect); const first = await repository.deployEffect(effect); const second = await repository.deployEffect(effect);
    assert.equal(first.revision, second.revision); assert.match(first.revision, /^sha256:[a-f0-9]{64}$/); assert.equal((await repository.loadEffect(effect.id)).schemaVersion, 2);
    const root = join(temporary, "radiantcursor-studio", "library", "effects", effect.id, "revisions", first.revision.slice(7));
    const runtime = JSON.parse(await readFile(join(root, "runtime.json"), "utf8")) as { revision: string; runtimeVersion: number };
    assert.equal(runtime.revision, first.revision); assert.equal(runtime.runtimeVersion, 1);
  } finally { await rm(temporary, { recursive: true, force: true }); }
});

test("formato .radiantcursor v2 faz round-trip e bloqueia caminhos inseguros", async () => {
  const sourceRoot = await mkdtemp(join(tmpdir(), "radiantcursor-export-")), destinationRoot = await mkdtemp(join(tmpdir(), "radiantcursor-import-"));
  try {
    const source = new EffectRepository(sourceRoot), destination = new EffectRepository(destinationRoot), effect = createBlankMotionEffect(); effect.id = "bundle-fixture";
    await source.saveDraft(effect); const imported = await destination.importEffect(await source.exportEffect(effect.id), "fixture.radiantcursor");
    assert.equal(imported.document.id, effect.id); assert.equal(imported.importedAssets, 0); assert.equal((await destination.loadEffect(effect.id)).metadata.name, effect.metadata.name);
    assert.throws(() => createZip(new Map([["../escape.json", Buffer.from("x")]])), /inseguro/i); assert.equal(readZip(createZip(new Map([["manifest.json", Buffer.from("{}")]]))).get("manifest.json")?.toString(), "{}");
  } finally { await Promise.all([rm(sourceRoot,{recursive:true,force:true}),rm(destinationRoot,{recursive:true,force:true})]); }
});
