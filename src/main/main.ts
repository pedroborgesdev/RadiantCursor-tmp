import { mkdirSync } from "node:fs";
import { readFile, stat, writeFile } from "node:fs/promises";
import { basename, isAbsolute, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import {
  app,
  BrowserWindow,
  dialog,
  ipcMain,
  nativeImage,
  type OpenDialogOptions,
  type SaveDialogOptions,
  type IpcMainInvokeEvent,
  session,
} from "electron";

import {
  RADIANT_CURSOR_IPC,
  type RadiantCursorState,
} from "../shared/types";
import { EffectRepository } from "./engine/effect-repository";
import { createRuntimeAdapter } from "./platform/create-runtime-adapter";
import { sharedConfigRoot, sharedDataRoot } from "./platform/paths";

const executableName = basename(process.execPath).replace(/\.exe$/i, "").toLowerCase();
const studioMode = process.argv.includes("--studio") ||
  process.env.RADIANTCURSOR_APP_MODE === "studio" ||
  executableName === "radiantcursor-studio" ||
  executableName === "radiantcursor studio";
const rendererTarget = studioMode ? "studio" : "normal";
const APP_NAME = studioMode ? "RadiantCursor Studio" : "RadiantCursor";
const controller = createRuntimeAdapter();
const effectRepository = new EffectRepository(sharedDataRoot(), true);
let mainWindow: BrowserWindow | null = null;

app.setName(APP_NAME);
if (process.platform === "linux") {
  app.setDesktopName(`${rendererTarget === "studio" ? "radiantcursor-studio" : "radiantcursor"}.desktop`);
}
const userDataPath = process.platform === "win32"
  ? join(sharedConfigRoot(), studioMode ? "studio-ui" : "normal-ui")
  : join(resolve(sharedConfigRoot(), ".."), studioMode ? "radiantcursor-studio" : "radiantcursor");
mkdirSync(userDataPath, { recursive: true, mode: 0o700 });
app.setPath("userData", userDataPath);

function getDevelopmentServerUrl(): URL | null {
  if (app.isPackaged || !process.env.VITE_DEV_SERVER_URL) {
    return null;
  }

  try {
    const url = new URL(process.env.VITE_DEV_SERVER_URL);
    const loopbackHosts = new Set(["127.0.0.1", "localhost", "[::1]"]);
    if (url.protocol !== "http:" || !loopbackHosts.has(url.hostname)) {
      throw new Error("the development server must use loopback HTTP");
    }
    return url;
  } catch (error) {
    throw new Error("VITE_DEV_SERVER_URL deve apontar para um servidor HTTP local.", {
      cause: error,
    });
  }
}

const developmentServerUrl = getDevelopmentServerUrl();
const rendererEntry = resolve(
  __dirname,
  "..",
  `renderer-${rendererTarget}`,
  `${rendererTarget}.html`,
);

function pathIsInside(candidate: string, parent: string): boolean {
  const relativePath = relative(parent, candidate);
  return relativePath === "" || (
    relativePath !== ".." &&
    !relativePath.startsWith(`..${process.platform === "win32" ? "\\" : "/"}`) &&
    !isAbsolute(relativePath)
  );
}

function isTrustedFrame(event: IpcMainInvokeEvent): boolean {
  if (!event.senderFrame || event.senderFrame !== event.sender.mainFrame) {
    return false;
  }

  try {
    const senderUrl = new URL(event.senderFrame.url);
    if (developmentServerUrl) {
      return senderUrl.origin === developmentServerUrl.origin;
    }

    if (senderUrl.protocol !== "file:") {
      return false;
    }
    const rendererDirectory = resolve(rendererEntry, "..");
    return pathIsInside(resolve(fileURLToPath(senderUrl)), rendererDirectory);
  } catch {
    return false;
  }
}

function assertTrustedFrame(event: IpcMainInvokeEvent): void {
  if (!isTrustedFrame(event)) {
    throw new Error("Solicitação IPC recusada para uma origem não confiável.");
  }
}

function broadcastState(state: RadiantCursorState): void {
  for (const window of BrowserWindow.getAllWindows()) {
    if (!window.isDestroyed() && !window.webContents.isDestroyed()) {
      window.webContents.send(RADIANT_CURSOR_IPC.stateChanged, state);
    }
  }
}

function registerIpcHandlers(): void {
  ipcMain.handle(RADIANT_CURSOR_IPC.windowMinimize, async (event) => {
    assertTrustedFrame(event);
    BrowserWindow.fromWebContents(event.sender)?.minimize();
  });

  ipcMain.handle(RADIANT_CURSOR_IPC.windowClose, async (event) => {
    assertTrustedFrame(event);
    BrowserWindow.fromWebContents(event.sender)?.close();
  });

  ipcMain.handle(RADIANT_CURSOR_IPC.getState, async (event) => {
    assertTrustedFrame(event);
    return controller.getState();
  });

  ipcMain.handle(RADIANT_CURSOR_IPC.checkCompatibility, async (event) => {
    assertTrustedFrame(event);
    return controller.checkCompatibility();
  });

  ipcMain.handle(
    RADIANT_CURSOR_IPC.applySettings,
    async (event, settings: unknown) => {
      assertTrustedFrame(event);
      const state = await controller.applySettings(settings);
      broadcastState(state);
      return state;
    },
  );

  ipcMain.handle(
    RADIANT_CURSOR_IPC.activateEffect,
    async (event, settings?: unknown) => {
      assertTrustedFrame(event);
      const state = await controller.activateEffect(settings);
      broadcastState(state);
      return state;
    },
  );

  ipcMain.handle(RADIANT_CURSOR_IPC.disableEffect, async (event) => {
    assertTrustedFrame(event);
    const state = await controller.disableEffect();
    broadcastState(state);
    return state;
  });

  ipcMain.handle(RADIANT_CURSOR_IPC.listEffects, async (event) => {
    assertTrustedFrame(event);
    return effectRepository.listEffects();
  });

  ipcMain.handle(RADIANT_CURSOR_IPC.loadEffect, async (event, id: unknown) => {
    assertTrustedFrame(event);
    return effectRepository.loadEffect(id);
  });

  ipcMain.handle(RADIANT_CURSOR_IPC.saveDraft, async (event, document: unknown) => {
    assertTrustedFrame(event);
    return effectRepository.saveDraft(document);
  });

  ipcMain.handle(RADIANT_CURSOR_IPC.deployEffect, async (event, document: unknown) => {
    assertTrustedFrame(event);
    const deployed = await effectRepository.deployEffect(document);
    await controller.activateEngineRevision(deployed.effectId, deployed.revision);
    broadcastState(await controller.getState());
    return deployed;
  });

  ipcMain.handle(RADIANT_CURSOR_IPC.deleteEffect, async (event, id: unknown) => {
    assertTrustedFrame(event);
    await effectRepository.deleteEffect(id);
  });

  ipcMain.handle(RADIANT_CURSOR_IPC.getRuntimeStatus, async (event) => {
    assertTrustedFrame(event);
    const active = await controller.readActiveEngineRevision();
    return effectRepository.getRuntimeStatus(active.effectId, active.revision);
  });

  ipcMain.handle(RADIANT_CURSOR_IPC.importImage, async (event) => {
    assertTrustedFrame(event);
    const options: OpenDialogOptions = {
      title: "Importar imagem para o efeito",
      properties: ["openFile"],
      filters: [{ name: "Imagens", extensions: ["png", "jpg", "jpeg", "webp", "svg"] }],
    };
    const selected = mainWindow
      ? await dialog.showOpenDialog(mainWindow, options)
      : await dialog.showOpenDialog(options);
    const source = selected.filePaths[0];
    if (selected.canceled || !source) return null;
    const fileStat = await stat(source);
    if (!fileStat.isFile() || fileStat.size > 32 * 1024 * 1024) throw new Error("Imagem inválida ou maior que 32 MiB.");
    let data = await readFile(source);
    let mediaType: "image/png" | "image/jpeg" | "image/webp";
    const prefix = data.subarray(0, 512).toString("utf8").trimStart().toLowerCase();
    if (prefix.startsWith("<svg") || prefix.startsWith("<?xml")) {
      const svg = nativeImage.createFromDataURL(`data:image/svg+xml;base64,${data.toString("base64")}`);
      if (svg.isEmpty()) throw new Error("O SVG não pôde ser rasterizado com segurança.");
      data = Buffer.from(svg.resize({ width: Math.min(2048, Math.max(64, svg.getSize().width || 512)), quality: "best" }).toPNG());
      mediaType = "image/png";
    } else if (data.subarray(0, 8).equals(Buffer.from([0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a]))) mediaType = "image/png";
    else if (data[0] === 0xff && data[1] === 0xd8 && data[2] === 0xff) mediaType = "image/jpeg";
    else if (data.subarray(0, 4).toString("ascii") === "RIFF" && data.subarray(8, 12).toString("ascii") === "WEBP") mediaType = "image/webp";
    else throw new Error("Formato de imagem não reconhecido.");
    const decoded = nativeImage.createFromBuffer(data);
    if (decoded.isEmpty()) throw new Error("A imagem está corrompida ou não pôde ser decodificada.");
    const size = decoded.getSize();
    return effectRepository.assetStore.importImage(data, mediaType, size.width, size.height);
  });

  ipcMain.handle(RADIANT_CURSOR_IPC.getAssetPreview, async (event, assetId: unknown) => {
    assertTrustedFrame(event);
    if (typeof assetId !== "string" || !/^sha256:[a-f0-9]{64}$/.test(assetId)) throw new Error("Asset inválido.");
    const asset = await effectRepository.assetStore.readAsset(assetId.slice(7));
    if (!asset || asset.data.length > 8 * 1024 * 1024) return null;
    const mediaType = asset.extension === "jpg" ? "image/jpeg" : asset.extension === "webp" ? "image/webp" : "image/png";
    return `data:${mediaType};base64,${asset.data.toString("base64")}`;
  });

  ipcMain.handle(RADIANT_CURSOR_IPC.exportEffect, async (event, id: unknown) => {
    assertTrustedFrame(event);
    if (typeof id !== "string") throw new Error("ID inválido.");
    const document = await effectRepository.loadEffect(id);
    const options: SaveDialogOptions = {
      title: "Exportar efeito RadiantCursor",
      defaultPath: `${document.metadata.name.replace(/[^a-zA-Z0-9._-]+/g, "-").toLowerCase() || "efeito"}.radiantcursor`,
      filters: [{ name: "RadiantCursor Effect", extensions: ["radiantcursor"] }],
    };
    const destination = mainWindow
      ? await dialog.showSaveDialog(mainWindow, options)
      : await dialog.showSaveDialog(options);
    if (destination.canceled || !destination.filePath) return null;
    await writeFile(destination.filePath, await effectRepository.exportEffect(id), { mode: 0o600, flag: "wx" });
    return destination.filePath;
  });

  ipcMain.handle(RADIANT_CURSOR_IPC.importEffect, async (event) => {
    assertTrustedFrame(event);
    const options: OpenDialogOptions = {
      title: "Importar efeito RadiantCursor",
      properties: ["openFile"],
      filters: [{ name: "RadiantCursor Effect", extensions: ["radiantcursor"] }],
    };
    const selected = mainWindow
      ? await dialog.showOpenDialog(mainWindow, options)
      : await dialog.showOpenDialog(options);
    const source = selected.filePaths[0];
    if (selected.canceled || !source) return null;
    const fileStat = await stat(source);
    if (!fileStat.isFile() || fileStat.size > 128 * 1024 * 1024) throw new Error("Bundle inválido ou maior que 128 MiB.");
    return effectRepository.importEffect(await readFile(source), source);
  });
}

function configureSessionSecurity(): void {
  session.defaultSession.setPermissionCheckHandler(() => false);
  session.defaultSession.setPermissionRequestHandler(
    (_webContents, _permission, callback) => callback(false),
  );

  if (app.isPackaged) {
    const contentSecurityPolicy = [
      "default-src 'self'",
      "script-src 'self'",
      "style-src 'self' 'unsafe-inline'",
      "img-src 'self' data:",
      "font-src 'self' data:",
      "connect-src 'self'",
      "object-src 'none'",
      "base-uri 'none'",
      "frame-src 'none'",
    ].join("; ");

    session.defaultSession.webRequest.onHeadersReceived((details, callback) => {
      callback({
        responseHeaders: {
          ...details.responseHeaders,
          "Content-Security-Policy": [contentSecurityPolicy],
        },
      });
    });
  }
}

async function createWindow(): Promise<BrowserWindow> {
  const iconFilename = studioMode ? "rcs-icon.png" : "rc-icon.png";
  const appIconPath = app.isPackaged
    ? join(process.resourcesPath, iconFilename)
    : join(app.getAppPath(), iconFilename);
  const window = new BrowserWindow({
    width: studioMode ? 1280 : 720,
    height: studioMode ? 800 : 620,
    minWidth: studioMode ? 1100 : 640,
    minHeight: studioMode ? 680 : 840,
    frame: false,
    maximizable: false,
    fullscreenable: false,
    show: false,
    backgroundColor: "#050505",
    icon: nativeImage.createFromPath(appIconPath),
    autoHideMenuBar: true,
    title: APP_NAME,
    webPreferences: {
      preload: join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      webSecurity: true,
      allowRunningInsecureContent: false,
      devTools: !app.isPackaged,
      spellcheck: false,
      webviewTag: false,
    },
  });

  window.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
  window.webContents.on("will-navigate", (event) => event.preventDefault());
  window.webContents.on("before-input-event", (event, input) => {
    if (input.key === "F11" || (input.alt && input.key === "Enter")) {
      event.preventDefault();
    }
  });
  window.on("maximize", () => window.unmaximize());
  window.on("enter-full-screen", () => window.setFullScreen(false));
  window.once("ready-to-show", () => window.show());
  window.on("closed", () => {
    if (mainWindow === window) {
      mainWindow = null;
    }
  });

  if (developmentServerUrl) {
    const url = new URL(`${rendererTarget}.html`, developmentServerUrl.href);
    await window.loadURL(url.href);
  } else {
    await window.loadFile(rendererEntry);
  }

  return window;
}

const hasSingleInstanceLock = app.requestSingleInstanceLock();

if (!hasSingleInstanceLock) {
  app.quit();
} else {
  app.on("second-instance", () => {
    if (mainWindow) {
      if (mainWindow.isMinimized()) {
        mainWindow.restore();
      }
      mainWindow.focus();
    }
  });

  void app.whenReady().then(async () => {
    await effectRepository.initialize();
    configureSessionSecurity();
    registerIpcHandlers();
    mainWindow = await createWindow();

    app.on("activate", () => {
      if (BrowserWindow.getAllWindows().length === 0) {
        void createWindow().then((window) => {
          mainWindow = window;
        });
      }
    });
  });
}

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") {
    app.quit();
  }
});
