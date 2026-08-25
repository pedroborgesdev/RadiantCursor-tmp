import { execFile } from "node:child_process";
import { constants as fsConstants } from "node:fs";
import {
  access,
  chmod,
  copyFile,
  mkdir,
  stat,
} from "node:fs/promises";
import { homedir } from "node:os";
import {
  delimiter,
  isAbsolute,
  join,
  resolve,
} from "node:path";

import {
  DEFAULT_RADIANT_CURSOR_SETTINGS,
  type RadiantCursorSettings,
  type RadiantCursorState,
  type RuntimeCompatibility,
} from "../../../shared/types";

const CONFIG_FILE = "kwinrc";
const EFFECT_GROUP = "Effect-radiantcursor";
const PLUGINS_GROUP = "Plugins";
const EFFECT_NAME = "radiantcursor";
const COMMAND_TIMEOUT_MS = 6_000;
const MAX_COMMAND_OUTPUT = 64 * 1024;

const SETTING_KEYS = [
  "ClickEnabled",
  "Color1",
  "Color2",
  "Color3",
  "LineWidth",
  "RingLife",
  "RingSize",
  "RingCount",
  "ShowText",
  "Font",
  "Style",
  "Trigger",
  "Glow",
  "TrailEnabled",
  "TrailStyle",
  "TrailColor",
  "TrailSize",
  "TrailLife",
  "TrailDensity",
  "TrailFrequency",
  "TrailOpacity",
  "TrailOffsetX",
  "TrailOffsetY",
  "CursorTextOffsetX",
  "CursorTextOffsetY",
  "CursorLinkOffsetX",
  "CursorLinkOffsetY",
  "CursorCrosshairOffsetX",
  "CursorCrosshairOffsetY",
  "CursorBusyOffsetX",
  "CursorBusyOffsetY",
  "CursorMoveOffsetX",
  "CursorMoveOffsetY",
  "CursorForbiddenOffsetX",
  "CursorForbiddenOffsetY",
  "CursorHelpOffsetX",
  "CursorHelpOffsetY",
  "CursorResizeHorizontalOffsetX",
  "CursorResizeHorizontalOffsetY",
  "CursorResizeVerticalOffsetX",
  "CursorResizeVerticalOffsetY",
  "CursorResizeDiagonalNwSeOffsetX",
  "CursorResizeDiagonalNwSeOffsetY",
  "CursorResizeDiagonalNeSwOffsetX",
  "CursorResizeDiagonalNeSwOffsetY",
  "TrailDistance",
  "TrailGlow",
  "TrailOnlyPressed",
  "HaloEnabled",
  "HaloStyle",
  "HaloColor",
  "HaloSize",
  "HaloDistance",
  "HaloDensity",
  "HaloOpacity",
  "HaloSpeed",
  "HaloVariantInterval",
  "HaloCycleVariants",
  "HaloGlow",
] as const satisfies readonly (keyof RadiantCursorSettings)[];

type SettingKey = (typeof SETTING_KEYS)[number];
type CommandKind = "reader" | "writer" | "qdbus";

const COMMAND_NAMES: Readonly<Record<CommandKind, readonly string[]>> = {
  reader: ["kreadconfig6", "kreadconfig5"],
  writer: ["kwriteconfig6", "kwriteconfig5"],
  qdbus: ["qdbus6", "qdbus-qt6", "qdbus"],
};

interface CommandOutput {
  stdout: string;
  stderr: string;
}

export class KWinIntegrationError extends Error {
  constructor(message: string, options?: ErrorOptions) {
    super(message, options);
    this.name = "KWinIntegrationError";
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function validateNumber(
  value: unknown,
  name: string,
  minimum: number,
  maximum: number,
  integer = false,
): number {
  if (
    typeof value !== "number" ||
    !Number.isFinite(value) ||
    value < minimum ||
    value > maximum ||
    (integer && !Number.isInteger(value))
  ) {
    const kind = integer ? "inteiro" : "número";
    throw new KWinIntegrationError(
      `${name} deve ser um ${kind} entre ${minimum} e ${maximum}.`,
    );
  }

  return value;
}

function validateColor(value: unknown, name: string): string {
  if (typeof value !== "string" || !/^#[0-9a-fA-F]{6}$/.test(value)) {
    throw new KWinIntegrationError(`${name} deve ser uma cor no formato #RRGGBB.`);
  }

  return value.toLowerCase();
}

function validateFont(value: unknown): string {
  if (typeof value !== "string") {
    throw new KWinIntegrationError("Font deve ser uma descrição de fonte válida.");
  }

  const font = value.trim();
  if (font.length === 0 || font.length > 256 || /[\u0000-\u001f\u007f]/.test(font)) {
    throw new KWinIntegrationError(
      "Font deve ter entre 1 e 256 caracteres e não pode conter caracteres de controle.",
    );
  }

  return font;
}

const EFFECT_STYLES = [
  "ripple",
  "pulse",
  "target",
  "burst",
  "spark",
  "focus",
  "halo",
  "shockwave",
  "orbit",
  "petals",
  "diamond",
  "sonar",
  "vortex",
  "cross",
  "confetti",
  "lightning",
  "bubbles",
  "heart",
  "ink",
  "splash",
  "nova",
  "comet",
  "eclipse",
  "plasma",
  "pixelburst",
  "prism",
  "flower",
  "meteor",
] as const;

const CLICK_TRIGGERS = ["press", "release", "both"] as const;

const TRAIL_STYLES = [
  "dots", "soft", "neon", "cometTrail", "smoke", "sparks",
  "bubbleTrail", "stars", "hearts", "squares", "diamonds",
  "triangles", "ribbon", "laser", "fire", "ice", "petalTrail",
  "pixels", "orbitTrail", "rainbow",
] as const;

function validateChoice<const Choice extends string>(
  value: unknown,
  name: string,
  choices: readonly Choice[],
): Choice {
  if (typeof value !== "string" || !choices.includes(value as Choice)) {
    throw new KWinIntegrationError(`${name} contém uma opção inválida.`);
  }
  return value as Choice;
}

/** Validate the renderer payload again at the privileged process boundary. */
export function validateRadiantCursorSettings(value: unknown): RadiantCursorSettings {
  if (!isRecord(value)) {
    throw new KWinIntegrationError("Configuração de clique inválida.");
  }

  const receivedKeys = Object.keys(value).sort();
  const expectedKeys = [...SETTING_KEYS].sort();
  if (
    receivedKeys.length !== expectedKeys.length ||
    receivedKeys.some((key, index) => key !== expectedKeys[index])
  ) {
    throw new KWinIntegrationError("A configuração contém campos ausentes ou desconhecidos.");
  }

  if (typeof value.ClickEnabled !== "boolean") {
    throw new KWinIntegrationError("ClickEnabled deve ser verdadeiro ou falso.");
  }
  if (typeof value.ShowText !== "boolean") {
    throw new KWinIntegrationError("ShowText deve ser verdadeiro ou falso.");
  }
  if (typeof value.Glow !== "boolean") {
    throw new KWinIntegrationError("Glow deve ser verdadeiro ou falso.");
  }
  if (typeof value.TrailEnabled !== "boolean" ||
      typeof value.TrailGlow !== "boolean" ||
      typeof value.TrailOnlyPressed !== "boolean") {
    throw new KWinIntegrationError("As opções booleanas do rastro são inválidas.");
  }
  if (typeof value.HaloEnabled !== "boolean" ||
      typeof value.HaloCycleVariants !== "boolean" ||
      typeof value.HaloGlow !== "boolean") {
    throw new KWinIntegrationError("As opções booleanas do halo são inválidas.");
  }

  return {
    ClickEnabled: value.ClickEnabled,
    Color1: validateColor(value.Color1, "Color1"),
    Color2: validateColor(value.Color2, "Color2"),
    Color3: validateColor(value.Color3, "Color3"),
    LineWidth: validateNumber(value.LineWidth, "LineWidth", 0, 99.99),
    RingLife: validateNumber(value.RingLife, "RingLife", 50, 5_000, true),
    RingSize: validateNumber(value.RingSize, "RingSize", 1, 1_000, true),
    RingCount: validateNumber(value.RingCount, "RingCount", 1, 99, true),
    ShowText: value.ShowText,
    Font: validateFont(value.Font),
    Style: validateChoice(value.Style, "Style", EFFECT_STYLES),
    Trigger: validateChoice(value.Trigger, "Trigger", CLICK_TRIGGERS),
    Glow: value.Glow,
    TrailEnabled: value.TrailEnabled,
    TrailStyle: validateChoice(value.TrailStyle, "TrailStyle", TRAIL_STYLES),
    TrailColor: validateColor(value.TrailColor, "TrailColor"),
    TrailSize: validateNumber(value.TrailSize, "TrailSize", 1, 200),
    TrailLife: validateNumber(value.TrailLife, "TrailLife", 50, 3_000, true),
    TrailDensity: validateNumber(value.TrailDensity, "TrailDensity", 1, 100, true),
    TrailFrequency: validateNumber(value.TrailFrequency, "TrailFrequency", 1, 240, true),
    TrailOpacity: validateNumber(value.TrailOpacity, "TrailOpacity", 0.05, 1),
    TrailOffsetX: validateNumber(value.TrailOffsetX, "TrailOffsetX", -128, 128),
    TrailOffsetY: validateNumber(value.TrailOffsetY, "TrailOffsetY", -128, 128),
    CursorTextOffsetX: validateNumber(value.CursorTextOffsetX, "CursorTextOffsetX", -128, 128),
    CursorTextOffsetY: validateNumber(value.CursorTextOffsetY, "CursorTextOffsetY", -128, 128),
    CursorLinkOffsetX: validateNumber(value.CursorLinkOffsetX, "CursorLinkOffsetX", -128, 128),
    CursorLinkOffsetY: validateNumber(value.CursorLinkOffsetY, "CursorLinkOffsetY", -128, 128),
    CursorCrosshairOffsetX: validateNumber(value.CursorCrosshairOffsetX, "CursorCrosshairOffsetX", -128, 128),
    CursorCrosshairOffsetY: validateNumber(value.CursorCrosshairOffsetY, "CursorCrosshairOffsetY", -128, 128),
    CursorBusyOffsetX: validateNumber(value.CursorBusyOffsetX, "CursorBusyOffsetX", -128, 128),
    CursorBusyOffsetY: validateNumber(value.CursorBusyOffsetY, "CursorBusyOffsetY", -128, 128),
    CursorMoveOffsetX: validateNumber(value.CursorMoveOffsetX, "CursorMoveOffsetX", -128, 128),
    CursorMoveOffsetY: validateNumber(value.CursorMoveOffsetY, "CursorMoveOffsetY", -128, 128),
    CursorForbiddenOffsetX: validateNumber(value.CursorForbiddenOffsetX, "CursorForbiddenOffsetX", -128, 128),
    CursorForbiddenOffsetY: validateNumber(value.CursorForbiddenOffsetY, "CursorForbiddenOffsetY", -128, 128),
    CursorHelpOffsetX: validateNumber(value.CursorHelpOffsetX, "CursorHelpOffsetX", -128, 128),
    CursorHelpOffsetY: validateNumber(value.CursorHelpOffsetY, "CursorHelpOffsetY", -128, 128),
    CursorResizeHorizontalOffsetX: validateNumber(value.CursorResizeHorizontalOffsetX, "CursorResizeHorizontalOffsetX", -128, 128),
    CursorResizeHorizontalOffsetY: validateNumber(value.CursorResizeHorizontalOffsetY, "CursorResizeHorizontalOffsetY", -128, 128),
    CursorResizeVerticalOffsetX: validateNumber(value.CursorResizeVerticalOffsetX, "CursorResizeVerticalOffsetX", -128, 128),
    CursorResizeVerticalOffsetY: validateNumber(value.CursorResizeVerticalOffsetY, "CursorResizeVerticalOffsetY", -128, 128),
    CursorResizeDiagonalNwSeOffsetX: validateNumber(value.CursorResizeDiagonalNwSeOffsetX, "CursorResizeDiagonalNwSeOffsetX", -128, 128),
    CursorResizeDiagonalNwSeOffsetY: validateNumber(value.CursorResizeDiagonalNwSeOffsetY, "CursorResizeDiagonalNwSeOffsetY", -128, 128),
    CursorResizeDiagonalNeSwOffsetX: validateNumber(value.CursorResizeDiagonalNeSwOffsetX, "CursorResizeDiagonalNeSwOffsetX", -128, 128),
    CursorResizeDiagonalNeSwOffsetY: validateNumber(value.CursorResizeDiagonalNeSwOffsetY, "CursorResizeDiagonalNeSwOffsetY", -128, 128),
    TrailDistance: validateNumber(value.TrailDistance, "TrailDistance", 0, 128),
    TrailGlow: value.TrailGlow,
    TrailOnlyPressed: value.TrailOnlyPressed,
    HaloEnabled: value.HaloEnabled,
    HaloStyle: validateChoice(value.HaloStyle, "HaloStyle", TRAIL_STYLES),
    HaloColor: validateColor(value.HaloColor, "HaloColor"),
    HaloSize: validateNumber(value.HaloSize, "HaloSize", 1, 200),
    HaloDistance: validateNumber(value.HaloDistance, "HaloDistance", 0, 256),
    HaloDensity: validateNumber(value.HaloDensity, "HaloDensity", 1, 100, true),
    HaloOpacity: validateNumber(value.HaloOpacity, "HaloOpacity", 0.05, 1),
    HaloSpeed: validateNumber(value.HaloSpeed, "HaloSpeed", 0, 4),
    HaloVariantInterval: validateNumber(value.HaloVariantInterval, "HaloVariantInterval", 10, 5_000, true),
    HaloCycleVariants: value.HaloCycleVariants,
    HaloGlow: value.HaloGlow,
  };
}

function commandErrorDetail(stderr: string): string {
  return stderr
    .replace(/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/g, "")
    .trim()
    .slice(0, 500);
}

function runCommand(file: string, args: readonly string[]): Promise<CommandOutput> {
  return new Promise((resolvePromise, rejectPromise) => {
    execFile(
      file,
      [...args],
      {
        encoding: "utf8",
        maxBuffer: MAX_COMMAND_OUTPUT,
        timeout: COMMAND_TIMEOUT_MS,
        windowsHide: true,
      },
      (error, stdout, stderr) => {
        if (error) {
          const detail = commandErrorDetail(stderr ?? "");
          rejectPromise(
            new KWinIntegrationError(
              detail
                ? `Falha ao executar ${file}: ${detail}`
                : `Falha ao executar ${file}.`,
              { cause: error },
            ),
          );
          return;
        }

        resolvePromise({ stdout: stdout ?? "", stderr: stderr ?? "" });
      },
    );
  });
}

function getConfigRoot(): string {
  const xdgConfigHome = process.env.XDG_CONFIG_HOME;
  return xdgConfigHome && isAbsolute(xdgConfigHome)
    ? resolve(xdgConfigHome)
    : join(homedir(), ".config");
}

function executableSearchDirectories(): string[] {
  const pathDirectories = (process.env.PATH ?? "/usr/local/bin:/usr/bin:/bin")
    .split(delimiter)
    .filter((directory) => directory.length > 0 && isAbsolute(directory));

  return [...new Set(["/usr/local/bin", "/usr/bin", "/bin", ...pathDirectories])];
}

async function isExecutable(filePath: string): Promise<boolean> {
  try {
    const fileStat = await stat(filePath);
    if (!fileStat.isFile()) {
      return false;
    }
    await access(filePath, fsConstants.X_OK);
    return true;
  } catch {
    return false;
  }
}

async function findCommand(kind: CommandKind): Promise<string | null> {
  for (const name of COMMAND_NAMES[kind]) {
    for (const directory of executableSearchDirectories()) {
      const candidate = join(directory, name);
      if (await isExecutable(candidate)) {
        return candidate;
      }
    }
  }

  return null;
}

function formatConfigValue(value: RadiantCursorSettings[SettingKey] | boolean): string {
  if (typeof value === "boolean") {
    return value ? "true" : "false";
  }
  return String(value);
}

export function buildWriteKeyArguments(group: string, key: string, value: string): string[] {
  return [
    "--file",
    CONFIG_FILE,
    "--group",
    group,
    "--key",
    key,
    "--",
    value,
  ];
}

function parseBoolean(value: string, fallback: boolean): boolean {
  const normalized = value.trim().toLowerCase();
  if (["true", "1", "yes", "on"].includes(normalized)) {
    return true;
  }
  if (["false", "0", "no", "off"].includes(normalized)) {
    return false;
  }
  return fallback;
}

function parseNumber(
  value: string,
  fallback: number,
  minimum: number,
  maximum: number,
  integer = false,
): number {
  const parsed = Number(value.trim());
  if (
    !Number.isFinite(parsed) ||
    parsed < minimum ||
    parsed > maximum ||
    (integer && !Number.isInteger(parsed))
  ) {
    return fallback;
  }
  return parsed;
}

function parseColor(value: string, fallback: string): string {
  const normalized = value.trim();
  return /^#[0-9a-fA-F]{6}$/.test(normalized)
    ? normalized.toLowerCase()
    : fallback;
}

function parseFont(value: string, fallback: string): string {
  try {
    return validateFont(value);
  } catch {
    return fallback;
  }
}

function timestampForFile(date: Date): string {
  const pad = (value: number): string => String(value).padStart(2, "0");
  return [
    date.getFullYear(),
    pad(date.getMonth() + 1),
    pad(date.getDate()),
    "-",
    pad(date.getHours()),
    pad(date.getMinutes()),
    pad(date.getSeconds()),
    "-",
    String(date.getMilliseconds()).padStart(3, "0"),
  ].join("");
}

async function fileExists(filePath: string): Promise<boolean> {
  try {
    await access(filePath, fsConstants.F_OK);
    return true;
  } catch {
    return false;
  }
}

function effectPluginCandidates(): string[] {
  const pluginRoots = (process.env.QT_PLUGIN_PATH ?? "")
    .split(delimiter)
    .filter((directory) => directory.length > 0 && isAbsolute(directory));
  const roots = [...new Set([
    ...pluginRoots,
    "/usr/lib/x86_64-linux-gnu/qt6/plugins",
    "/usr/lib/aarch64-linux-gnu/qt6/plugins",
    "/usr/lib/qt6/plugins",
    "/usr/local/lib/qt6/plugins",
  ])];
  const relativeCandidates = [
    join("kwin", "effects", "plugins", "radiantcursor.so"),
    join("kwin", "effects", "plugins", "libradiantcursor.so"),
  ];

  return roots.flatMap((root) =>
    relativeCandidates.map((relativePath) => join(root, relativePath)),
  );
}

export class KWinController {
  private backupPromise: Promise<string | null> | null = null;
  private backupPath: string | null = null;
  private mutationTail: Promise<void> = Promise.resolve();

  private enqueueMutation<T>(operation: () => Promise<T>): Promise<T> {
    const pending = this.mutationTail.then(operation, operation);
    this.mutationTail = pending.then(
      () => undefined,
      () => undefined,
    );
    return pending;
  }

  private async requireCommand(kind: CommandKind): Promise<string> {
    const command = await findCommand(kind);
    if (command) {
      return command;
    }

    const names = COMMAND_NAMES[kind].join("/");
    throw new KWinIntegrationError(`Comando necessário não encontrado: ${names}.`);
  }

  private async readKey(
    reader: string,
    group: string,
    key: string,
    defaultValue: string,
  ): Promise<string> {
    const result = await runCommand(reader, [
      "--file",
      CONFIG_FILE,
      "--group",
      group,
      "--key",
      key,
      "--default",
      defaultValue,
    ]);
    return result.stdout.trim() || defaultValue;
  }

  private async writeKey(
    writer: string,
    group: string,
    key: string,
    value: string,
  ): Promise<void> {
    await runCommand(writer, buildWriteKeyArguments(group, key, value));
  }

  private async createSessionBackup(): Promise<string | null> {
    const configRoot = getConfigRoot();
    const source = join(configRoot, CONFIG_FILE);
    if (!(await fileExists(source))) {
      return null;
    }

    const backupDirectory = join(configRoot, "radiantcursor-studio", "backups");
    await mkdir(backupDirectory, { recursive: true, mode: 0o700 });

    const destination = join(
      backupDirectory,
      `kwinrc.${timestampForFile(new Date())}.${process.pid}.bak`,
    );
    await copyFile(source, destination, fsConstants.COPYFILE_EXCL);
    await chmod(destination, 0o600);
    this.backupPath = destination;
    return destination;
  }

  private ensureSessionBackup(): Promise<string | null> {
    if (!this.backupPromise) {
      this.backupPromise = this.createSessionBackup().catch((error: unknown) => {
        this.backupPromise = null;
        throw new KWinIntegrationError(
          "Não foi possível criar o backup do kwinrc; nenhuma alteração foi feita.",
          { cause: error },
        );
      });
    }
    return this.backupPromise;
  }

  private async writeSettings(settings: RadiantCursorSettings): Promise<void> {
    const writer = await this.requireCommand("writer");
    await this.ensureSessionBackup();

    for (const key of SETTING_KEYS) {
      await this.writeKey(
        writer,
        EFFECT_GROUP,
        key,
        formatConfigValue(settings[key]),
      );
    }
  }

  private async clearActiveEngineRevision(): Promise<void> {
    const writer = await this.requireCommand("writer");
    await this.ensureSessionBackup();
    await this.writeKey(writer, EFFECT_GROUP, "ActiveEffectId", "");
    await this.writeKey(writer, EFFECT_GROUP, "ActiveRevision", "");
    await this.writeKey(writer, EFFECT_GROUP, "ActiveHaloEffectId", "");
    await this.writeKey(writer, EFFECT_GROUP, "ActiveHaloRevision", "");
  }

  private async setPluginEnabled(enabled: boolean): Promise<void> {
    const writer = await this.requireCommand("writer");
    await this.ensureSessionBackup();
    await this.writeKey(
      writer,
      PLUGINS_GROUP,
      "radiantcursorEnabled",
      formatConfigValue(enabled),
    );
  }

  private async disableNativeMouseClick(): Promise<void> {
    const writer = await this.requireCommand("writer");
    await this.ensureSessionBackup();
    await this.writeKey(writer, PLUGINS_GROUP, "mouseclickEnabled", "false");
  }

  private async callEffectMethod(
    method: string,
    effectName = EFFECT_NAME,
  ): Promise<void> {
    const qdbus = await this.requireCommand("qdbus");
    const result = await runCommand(qdbus, [
      "org.kde.KWin",
      "/Effects",
      `org.kde.kwin.Effects.${method}`,
      effectName,
    ]);
    if (result.stdout.trim().toLowerCase() === "false") {
      if (method === "loadEffect" && effectName === EFFECT_NAME) {
        throw new KWinIntegrationError(
          "O KWin não reconheceu o plugin RadiantCursor. Reinstale-o com ./install-kwin-effect.sh e reinicie somente o compositor com kwin_wayland --replace.",
        );
      }
      throw new KWinIntegrationError(
        `O KWin recusou a operação ${method} para o efeito ${effectName}.`,
      );
    }
  }

  private async tryEffectMethod(
    method: string,
    effectName = EFFECT_NAME,
  ): Promise<boolean> {
    try {
      await this.callEffectMethod(method, effectName);
      return true;
    } catch {
      return false;
    }
  }

  private async effectIsLoaded(pluginEnabled: boolean): Promise<boolean> {
    const qdbus = await findCommand("qdbus");
    if (!qdbus) {
      return pluginEnabled;
    }

    try {
      const result = await runCommand(qdbus, [
        "org.kde.KWin",
        "/Effects",
        "org.kde.kwin.Effects.isEffectLoaded",
        EFFECT_NAME,
      ]);
      return parseBoolean(result.stdout, pluginEnabled);
    } catch {
      return pluginEnabled;
    }
  }

  private async effectIsSupported(): Promise<boolean> {
    const qdbus = await findCommand("qdbus");
    if (!qdbus) return false;
    try {
      const result = await runCommand(qdbus, [
        "org.kde.KWin",
        "/Effects",
        "org.kde.kwin.Effects.isEffectSupported",
        EFFECT_NAME,
      ]);
      return parseBoolean(result.stdout, false);
    } catch {
      return false;
    }
  }

  async readSettings(): Promise<RadiantCursorSettings> {
    const reader = await findCommand("reader");
    if (!reader) {
      return { ...DEFAULT_RADIANT_CURSOR_SETTINGS };
    }

    const defaults = DEFAULT_RADIANT_CURSOR_SETTINGS;
    const values = await Promise.all(
      SETTING_KEYS.map((key) =>
        this.readKey(reader, EFFECT_GROUP, key, formatConfigValue(defaults[key])),
      ),
    );
    const raw = Object.fromEntries(
      SETTING_KEYS.map((key, index) => [key, values[index]]),
    ) as Record<SettingKey, string>;

    return {
      ClickEnabled: parseBoolean(raw.ClickEnabled, defaults.ClickEnabled),
      Color1: parseColor(raw.Color1, defaults.Color1),
      Color2: parseColor(raw.Color2, defaults.Color2),
      Color3: parseColor(raw.Color3, defaults.Color3),
      LineWidth: parseNumber(raw.LineWidth, defaults.LineWidth, 0, 99.99),
      RingLife: parseNumber(raw.RingLife, defaults.RingLife, 50, 5_000, true),
      RingSize: parseNumber(raw.RingSize, defaults.RingSize, 1, 1_000, true),
      RingCount: parseNumber(raw.RingCount, defaults.RingCount, 1, 99, true),
      ShowText: parseBoolean(raw.ShowText, defaults.ShowText),
      Font: parseFont(raw.Font, defaults.Font),
      Style: EFFECT_STYLES.includes(raw.Style as (typeof EFFECT_STYLES)[number])
        ? raw.Style as RadiantCursorSettings["Style"]
        : defaults.Style,
      Trigger: CLICK_TRIGGERS.includes(raw.Trigger as (typeof CLICK_TRIGGERS)[number])
        ? raw.Trigger as RadiantCursorSettings["Trigger"]
        : defaults.Trigger,
      Glow: parseBoolean(raw.Glow, defaults.Glow),
      TrailEnabled: parseBoolean(raw.TrailEnabled, defaults.TrailEnabled),
      TrailStyle: TRAIL_STYLES.includes(raw.TrailStyle as (typeof TRAIL_STYLES)[number])
        ? raw.TrailStyle as RadiantCursorSettings["TrailStyle"]
        : defaults.TrailStyle,
      TrailColor: parseColor(raw.TrailColor, defaults.TrailColor),
      TrailSize: parseNumber(raw.TrailSize, defaults.TrailSize, 1, 200),
      TrailLife: parseNumber(raw.TrailLife, defaults.TrailLife, 50, 3_000, true),
      TrailDensity: parseNumber(raw.TrailDensity, defaults.TrailDensity, 1, 100, true),
      TrailFrequency: parseNumber(raw.TrailFrequency, defaults.TrailFrequency, 1, 240, true),
      TrailOpacity: parseNumber(raw.TrailOpacity, defaults.TrailOpacity, 0.05, 1),
      TrailOffsetX: parseNumber(raw.TrailOffsetX, defaults.TrailOffsetX, -128, 128),
      TrailOffsetY: parseNumber(raw.TrailOffsetY, defaults.TrailOffsetY, -128, 128),
      CursorTextOffsetX: parseNumber(raw.CursorTextOffsetX, defaults.CursorTextOffsetX, -128, 128),
      CursorTextOffsetY: parseNumber(raw.CursorTextOffsetY, defaults.CursorTextOffsetY, -128, 128),
      CursorLinkOffsetX: parseNumber(raw.CursorLinkOffsetX, defaults.CursorLinkOffsetX, -128, 128),
      CursorLinkOffsetY: parseNumber(raw.CursorLinkOffsetY, defaults.CursorLinkOffsetY, -128, 128),
      CursorCrosshairOffsetX: parseNumber(raw.CursorCrosshairOffsetX, defaults.CursorCrosshairOffsetX, -128, 128),
      CursorCrosshairOffsetY: parseNumber(raw.CursorCrosshairOffsetY, defaults.CursorCrosshairOffsetY, -128, 128),
      CursorBusyOffsetX: parseNumber(raw.CursorBusyOffsetX, defaults.CursorBusyOffsetX, -128, 128),
      CursorBusyOffsetY: parseNumber(raw.CursorBusyOffsetY, defaults.CursorBusyOffsetY, -128, 128),
      CursorMoveOffsetX: parseNumber(raw.CursorMoveOffsetX, defaults.CursorMoveOffsetX, -128, 128),
      CursorMoveOffsetY: parseNumber(raw.CursorMoveOffsetY, defaults.CursorMoveOffsetY, -128, 128),
      CursorForbiddenOffsetX: parseNumber(raw.CursorForbiddenOffsetX, defaults.CursorForbiddenOffsetX, -128, 128),
      CursorForbiddenOffsetY: parseNumber(raw.CursorForbiddenOffsetY, defaults.CursorForbiddenOffsetY, -128, 128),
      CursorHelpOffsetX: parseNumber(raw.CursorHelpOffsetX, defaults.CursorHelpOffsetX, -128, 128),
      CursorHelpOffsetY: parseNumber(raw.CursorHelpOffsetY, defaults.CursorHelpOffsetY, -128, 128),
      CursorResizeHorizontalOffsetX: parseNumber(raw.CursorResizeHorizontalOffsetX, defaults.CursorResizeHorizontalOffsetX, -128, 128),
      CursorResizeHorizontalOffsetY: parseNumber(raw.CursorResizeHorizontalOffsetY, defaults.CursorResizeHorizontalOffsetY, -128, 128),
      CursorResizeVerticalOffsetX: parseNumber(raw.CursorResizeVerticalOffsetX, defaults.CursorResizeVerticalOffsetX, -128, 128),
      CursorResizeVerticalOffsetY: parseNumber(raw.CursorResizeVerticalOffsetY, defaults.CursorResizeVerticalOffsetY, -128, 128),
      CursorResizeDiagonalNwSeOffsetX: parseNumber(raw.CursorResizeDiagonalNwSeOffsetX, defaults.CursorResizeDiagonalNwSeOffsetX, -128, 128),
      CursorResizeDiagonalNwSeOffsetY: parseNumber(raw.CursorResizeDiagonalNwSeOffsetY, defaults.CursorResizeDiagonalNwSeOffsetY, -128, 128),
      CursorResizeDiagonalNeSwOffsetX: parseNumber(raw.CursorResizeDiagonalNeSwOffsetX, defaults.CursorResizeDiagonalNeSwOffsetX, -128, 128),
      CursorResizeDiagonalNeSwOffsetY: parseNumber(raw.CursorResizeDiagonalNeSwOffsetY, defaults.CursorResizeDiagonalNeSwOffsetY, -128, 128),
      TrailDistance: parseNumber(raw.TrailDistance, defaults.TrailDistance, 0, 128),
      TrailGlow: parseBoolean(raw.TrailGlow, defaults.TrailGlow),
      TrailOnlyPressed: parseBoolean(raw.TrailOnlyPressed, defaults.TrailOnlyPressed),
      HaloEnabled: parseBoolean(raw.HaloEnabled, defaults.HaloEnabled),
      HaloStyle: TRAIL_STYLES.includes(raw.HaloStyle as (typeof TRAIL_STYLES)[number])
        ? raw.HaloStyle as RadiantCursorSettings["HaloStyle"]
        : defaults.HaloStyle,
      HaloColor: parseColor(raw.HaloColor, defaults.HaloColor),
      HaloSize: parseNumber(raw.HaloSize, defaults.HaloSize, 1, 200),
      HaloDistance: parseNumber(raw.HaloDistance, defaults.HaloDistance, 0, 256),
      HaloDensity: parseNumber(raw.HaloDensity, defaults.HaloDensity, 1, 100, true),
      HaloOpacity: parseNumber(raw.HaloOpacity, defaults.HaloOpacity, 0.05, 1),
      HaloSpeed: parseNumber(raw.HaloSpeed, defaults.HaloSpeed, 0, 4),
      HaloVariantInterval: parseNumber(raw.HaloVariantInterval, defaults.HaloVariantInterval, 10, 5_000, true),
      HaloCycleVariants: parseBoolean(raw.HaloCycleVariants, defaults.HaloCycleVariants),
      HaloGlow: parseBoolean(raw.HaloGlow, defaults.HaloGlow),
    };
  }

  private async readPluginEnabled(): Promise<boolean> {
    const reader = await findCommand("reader");
    if (!reader) {
      return false;
    }
    const raw = await this.readKey(
      reader,
      PLUGINS_GROUP,
      "radiantcursorEnabled",
      "false",
    );
    return parseBoolean(raw, false);
  }

  async checkCompatibility(): Promise<RuntimeCompatibility> {
    const [reader, writer, qdbus, effectInstalled] = await Promise.all([
      findCommand("reader"),
      findCommand("writer"),
      findCommand("qdbus"),
      Promise.any(
        effectPluginCandidates().map(async (candidate) => {
          if (await fileExists(candidate)) {
            return true;
          }
          throw new Error("not found");
        }),
      ).catch(() => false),
    ]);

    const configReadable = reader !== null;
    const configWritable = writer !== null;
    let dbusAvailable = false;
    let effectDiscovered = false;
    if (qdbus) {
      try {
        const support = await runCommand(qdbus, [
          "org.kde.KWin", "/Effects",
          "org.kde.kwin.Effects.isEffectSupported", EFFECT_NAME,
        ]);
        dbusAvailable = true;
        effectDiscovered = parseBoolean(support.stdout, false);
      } catch {
        dbusAvailable = false;
      }
    }
    const details: string[] = [];

    if (!configReadable) {
      details.push("kreadconfig6/5 não foi encontrado.");
    }
    if (!configWritable) {
      details.push("kwriteconfig6/5 não foi encontrado.");
    }
    if (!qdbus) {
      details.push("qdbus6/qdbus-qt6/qdbus não foi encontrado.");
    } else if (!dbusAvailable) {
      details.push("Não foi possível acessar o serviço D-Bus do KWin nesta sessão.");
    }
    if (!effectInstalled) {
      details.push("O plugin RadiantCursor do KWin não foi localizado. Execute o instalador do efeito.");
    } else if (dbusAvailable && !effectDiscovered) {
      details.push("O arquivo do plugin existe, mas o KWin ainda não reconheceu sua factory. Reinstale o efeito e reinicie somente o KWin.");
    }

    return {
      platform: "kwin",
      compatible:
        configReadable && configWritable && dbusAvailable && effectInstalled && effectDiscovered,
      runtimeInstalled: effectInstalled,
      transportAvailable: dbusAvailable,
      configReadable,
      configWritable,
      dbusAvailable,
      effectInstalled,
      effectDiscovered,
      details,
    };
  }

  async getState(): Promise<RadiantCursorState> {
    const [settings, pluginEnabled, compatibility] = await Promise.all([
      this.readSettings(),
      this.readPluginEnabled(),
      this.checkCompatibility(),
    ]);
    const isLoaded = await this.effectIsLoaded(pluginEnabled);

    return {
      settings,
      isLoaded,
      compatibility,
      backupPath: this.backupPath,
    };
  }

  async readActiveEngineRevision(target: "click" | "halo" = "click"): Promise<{ effectId: string | null; revision: string | null }> {
    const reader = await findCommand("reader");
    if (!reader) return { effectId: null, revision: null };
    const idKey = target === "halo" ? "ActiveHaloEffectId" : "ActiveEffectId";
    const revisionKey = target === "halo" ? "ActiveHaloRevision" : "ActiveRevision";
    const [effectId, revision] = await Promise.all([
      this.readKey(reader, EFFECT_GROUP, idKey, ""),
      this.readKey(reader, EFFECT_GROUP, revisionKey, ""),
    ]);
    return { effectId: effectId || null, revision: revision || null };
  }

  activateEngineRevision(effectId: string, revision: string, target: "click" | "halo" = "click"): Promise<void> {
    if (!/^[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}$/.test(effectId) || !/^sha256:[a-f0-9]{64}$/.test(revision)) {
      throw new KWinIntegrationError("Efeito ou revisão declarativa inválida.");
    }
    return this.enqueueMutation(async () => {
      const writer = await this.requireCommand("writer");
      await this.ensureSessionBackup();
      await this.writeKey(writer, EFFECT_GROUP, target === "halo" ? "ActiveHaloEffectId" : "ActiveEffectId", effectId);
      await this.writeKey(writer, EFFECT_GROUP, target === "halo" ? "ActiveHaloRevision" : "ActiveRevision", revision);
      await this.setPluginEnabled(true);
      await this.disableNativeMouseClick();
      await this.tryEffectMethod("unloadEffect", "mouseclick");
      const loaded = await this.effectIsLoaded(true);
      if (!loaded) {
        if (!await this.effectIsSupported()) {
          throw new KWinIntegrationError(
            "O KWin não reconheceu o plugin RadiantCursor instalado. Execute ./install-kwin-effect.sh e reinicie somente o compositor com kwin_wayland --replace.",
          );
        }
        await this.callEffectMethod("loadEffect");
      }
      await this.callEffectMethod("reconfigureEffect");
    });
  }

  applySettings(value: unknown): Promise<RadiantCursorState> {
    const settings = validateRadiantCursorSettings(value);
    return this.enqueueMutation(async () => {
      await this.writeSettings(settings);
      await this.clearActiveEngineRevision();
      // Persisting settings remains useful even when live DBus reload is unavailable.
      await this.tryEffectMethod("reconfigureEffect");
      return this.getState();
    });
  }

  activateEffect(value?: unknown): Promise<RadiantCursorState> {
    const settings = value === undefined ? undefined : validateRadiantCursorSettings(value);
    return this.enqueueMutation(async () => {
      if (settings) {
        await this.writeSettings(settings);
        await this.clearActiveEngineRevision();
      }
      await this.setPluginEnabled(true);
      await this.disableNativeMouseClick();

      await this.tryEffectMethod("unloadEffect", "mouseclick");
      await this.tryEffectMethod("unloadEffect");
      await this.callEffectMethod("loadEffect");
      await this.callEffectMethod("reconfigureEffect");
      return this.getState();
    });
  }

  disableEffect(): Promise<RadiantCursorState> {
    return this.enqueueMutation(async () => {
      await this.setPluginEnabled(false);
      await this.tryEffectMethod("unloadEffect");
      return this.getState();
    });
  }
}
