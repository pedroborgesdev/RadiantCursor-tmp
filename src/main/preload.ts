import { contextBridge, ipcRenderer } from "electron";

import {
  RADIANT_CURSOR_IPC,
  type RadiantCursorApi,
  type RadiantCursorSettings,
  type RadiantCursorState,
} from "../shared/types";
import type { EffectDocumentV2 } from "../shared/motion/schema";

const radiantCursorApi: RadiantCursorApi = Object.freeze({
  minimizeWindow: () => ipcRenderer.invoke(RADIANT_CURSOR_IPC.windowMinimize),
  closeWindow: () => ipcRenderer.invoke(RADIANT_CURSOR_IPC.windowClose),
  getState: () => ipcRenderer.invoke(RADIANT_CURSOR_IPC.getState),
  applySettings: (settings: RadiantCursorSettings) =>
    ipcRenderer.invoke(RADIANT_CURSOR_IPC.applySettings, settings),
  activateEffect: (settings?: RadiantCursorSettings) =>
    ipcRenderer.invoke(RADIANT_CURSOR_IPC.activateEffect, settings),
  disableEffect: () => ipcRenderer.invoke(RADIANT_CURSOR_IPC.disableEffect),
  checkCompatibility: () => ipcRenderer.invoke(RADIANT_CURSOR_IPC.checkCompatibility),
  listEffects: () => ipcRenderer.invoke(RADIANT_CURSOR_IPC.listEffects),
  loadEffect: (id: string) => ipcRenderer.invoke(RADIANT_CURSOR_IPC.loadEffect, id),
  saveDraft: (document: EffectDocumentV2) => ipcRenderer.invoke(RADIANT_CURSOR_IPC.saveDraft, document),
  deployEffect: (document: EffectDocumentV2) => ipcRenderer.invoke(RADIANT_CURSOR_IPC.deployEffect, document),
  deleteEffect: (id: string) => ipcRenderer.invoke(RADIANT_CURSOR_IPC.deleteEffect, id),
  getRuntimeStatus: () => ipcRenderer.invoke(RADIANT_CURSOR_IPC.getRuntimeStatus),
  importImage: () => ipcRenderer.invoke(RADIANT_CURSOR_IPC.importImage),
  getAssetPreview: (assetId: string) => ipcRenderer.invoke(RADIANT_CURSOR_IPC.getAssetPreview, assetId),
  exportEffect: (id: string) => ipcRenderer.invoke(RADIANT_CURSOR_IPC.exportEffect, id),
  importEffect: () => ipcRenderer.invoke(RADIANT_CURSOR_IPC.importEffect),
  onStateChanged: (listener: (state: RadiantCursorState) => void) => {
    if (typeof listener !== "function") {
      throw new TypeError("O listener de estado deve ser uma função.");
    }

    const wrappedListener = (_event: Electron.IpcRendererEvent, state: RadiantCursorState) => {
      listener(state);
    };
    ipcRenderer.on(RADIANT_CURSOR_IPC.stateChanged, wrappedListener);

    return () => {
      ipcRenderer.removeListener(RADIANT_CURSOR_IPC.stateChanged, wrappedListener);
    };
  },
});

contextBridge.exposeInMainWorld("radiantcursor", radiantCursorApi);
