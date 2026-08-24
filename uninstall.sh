#!/usr/bin/env bash
set -Eeuo pipefail

readonly APP_NAME="RadiantCursor e RadiantCursor Studio"
readonly STUDIO_ID="radiantcursor-studio"
readonly NORMAL_ID="radiantcursor"

USER_HOME="${HOME:-}"
DATA_HOME="$USER_HOME/.local/share"
CONFIG_HOME="${XDG_CONFIG_HOME:-$USER_HOME/.config}"
STUDIO_APP_ROOT="$DATA_HOME/$STUDIO_ID"
STUDIO_APP_DIR="$STUDIO_APP_ROOT/app"
NORMAL_APP_ROOT="$DATA_HOME/$NORMAL_ID"
NORMAL_APP_DIR="$NORMAL_APP_ROOT/app"
LEGACY_APP="$STUDIO_APP_ROOT/radiantcursor-studio.py"
STUDIO_LAUNCHER="$USER_HOME/.local/bin/$STUDIO_ID"
NORMAL_LAUNCHER="$USER_HOME/.local/bin/$NORMAL_ID"
APPLICATIONS_DIR="$DATA_HOME/applications"
STUDIO_DESKTOP_FILE="$APPLICATIONS_DIR/$STUDIO_ID.desktop"
NORMAL_DESKTOP_FILE="$APPLICATIONS_DIR/$NORMAL_ID.desktop"
STUDIO_PRESERVED_DIR="$CONFIG_HOME/$STUDIO_ID"
NORMAL_PRESERVED_DIR="$CONFIG_HOME/$NORMAL_ID"
PLUGIN_ROOT="$(qtpaths6 --plugin-dir 2>/dev/null || true)"
PLUGIN_FILE="${PLUGIN_ROOT:+$PLUGIN_ROOT/kwin/effects/plugins/radiantcursor.so}"

die() {
    printf 'Erro: %s\n' "$*" >&2
    exit 1
}

[[ -n "$USER_HOME" && -d "$USER_HOME" ]] || die "a variável HOME não aponta para um diretório válido."
[[ "$CONFIG_HOME" == /* ]] || die "XDG_CONFIG_HOME precisa ser um caminho absoluto."
(( EUID != 0 )) || die "não execute este desinstalador com sudo ou como root."

removed=false

if [[ -e "$STUDIO_APP_DIR" || -L "$STUDIO_APP_DIR" ]]; then
    rm -rf -- "$STUDIO_APP_DIR"
    removed=true
fi
if [[ -e "$NORMAL_APP_DIR" || -L "$NORMAL_APP_DIR" ]]; then
    rm -rf -- "$NORMAL_APP_DIR"
    removed=true
fi
if [[ -e "$LEGACY_APP" || -L "$LEGACY_APP" ]]; then
    rm -f -- "$LEGACY_APP"
    removed=true
fi
if [[ -e "$STUDIO_LAUNCHER" || -L "$STUDIO_LAUNCHER" ]]; then
    rm -f -- "$STUDIO_LAUNCHER"
    removed=true
fi
if [[ -e "$STUDIO_DESKTOP_FILE" || -L "$STUDIO_DESKTOP_FILE" ]]; then
    rm -f -- "$STUDIO_DESKTOP_FILE"
    removed=true
fi
if [[ -e "$NORMAL_LAUNCHER" || -L "$NORMAL_LAUNCHER" ]]; then
    rm -f -- "$NORMAL_LAUNCHER"
    removed=true
fi
if [[ -e "$NORMAL_DESKTOP_FILE" || -L "$NORMAL_DESKTOP_FILE" ]]; then
    rm -f -- "$NORMAL_DESKTOP_FILE"
    removed=true
fi

if [[ -n "$PLUGIN_FILE" && -f "$PLUGIN_FILE" ]]; then
    printf 'O plugin global do KWin também será removido (sudo necessário).\n'
    sudo rm -f -- "$PLUGIN_FILE"
    removed=true
fi

# Só remove a pasta do aplicativo se ela estiver vazia; arquivos desconhecidos
# ou restos de uma recuperação manual são preservados para inspeção.
rmdir -- "$STUDIO_APP_ROOT" 2>/dev/null || true
rmdir -- "$NORMAL_APP_ROOT" 2>/dev/null || true

if command -v update-desktop-database >/dev/null 2>&1 && [[ -d "$APPLICATIONS_DIR" ]]; then
    update-desktop-database "$APPLICATIONS_DIR" >/dev/null 2>&1 || true
fi
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
fi

if [[ "$removed" == true ]]; then
    printf '%s removido.\n' "$APP_NAME"
else
    printf '%s não estava instalado nos caminhos locais esperados.\n' "$APP_NAME"
fi
printf 'Configuração do RadiantCursor preservada em: %s\n' "$NORMAL_PRESERVED_DIR"
printf 'Configuração do Studio e backups preservados em: %s\n' "$STUDIO_PRESERVED_DIR"
printf 'A configuração do KWin também não foi alterada.\n'
