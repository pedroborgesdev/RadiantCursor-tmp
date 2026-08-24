#!/usr/bin/env bash
set -Eeuo pipefail

readonly SUITE_NAME="RadiantCursor e RadiantCursor Studio"
readonly NORMAL_ID="radiantcursor"
readonly STUDIO_ID="radiantcursor-studio"
readonly MIN_NODE_MAJOR=22
readonly MIN_NODE_MINOR=12
readonly MIN_NPM_MAJOR=10

SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
USER_HOME="${HOME:-}"
DATA_HOME="$USER_HOME/.local/share"
CONFIG_HOME="${XDG_CONFIG_HOME:-$USER_HOME/.config}"
BIN_DIR="$USER_HOME/.local/bin"
APPLICATIONS_DIR="$DATA_HOME/applications"

NORMAL_APP_ROOT="$DATA_HOME/$NORMAL_ID"
NORMAL_APP_DIR="$NORMAL_APP_ROOT/app"
STUDIO_APP_ROOT="$DATA_HOME/$STUDIO_ID"
STUDIO_APP_DIR="$STUDIO_APP_ROOT/app"
NORMAL_LAUNCHER="$BIN_DIR/$NORMAL_ID"
STUDIO_LAUNCHER="$BIN_DIR/$STUDIO_ID"
NORMAL_DESKTOP_FILE="$APPLICATIONS_DIR/$NORMAL_ID.desktop"
STUDIO_DESKTOP_FILE="$APPLICATIONS_DIR/$STUDIO_ID.desktop"
LEGACY_APP="$STUDIO_APP_ROOT/radiantcursor-studio.py"
BACKUP_ROOT="$CONFIG_HOME/$STUDIO_ID/backups"

NORMAL_PACKAGE_OUTPUT="$SOURCE_DIR/release/$NORMAL_ID/linux-unpacked"
STUDIO_PACKAGE_OUTPUT="$SOURCE_DIR/release/$STUDIO_ID/linux-unpacked"
NORMAL_PACKAGE_BINARY="$NORMAL_PACKAGE_OUTPUT/$NORMAL_ID"
STUDIO_PACKAGE_BINARY="$STUDIO_PACKAGE_OUTPUT/$STUDIO_ID"

STAGE_ROOT=""
NORMAL_LAUNCHER_TEMP=""
STUDIO_LAUNCHER_TEMP=""
NORMAL_DESKTOP_TEMP=""
STUDIO_DESKTOP_TEMP=""
NORMAL_PREVIOUS_APP=""
STUDIO_PREVIOUS_APP=""
NORMAL_SWAPPED=false
STUDIO_SWAPPED=false
INSTALL_BACKUP=""

info() { printf '  %s\n' "$*"; }
warn() { printf 'Aviso: %s\n' "$*" >&2; }
die() { printf 'Erro: %s\n' "$*" >&2; exit 1; }

restore_file() {
    local backup_name="$1"
    local destination="$2"
    if [[ -n "$INSTALL_BACKUP" && ( -e "$INSTALL_BACKUP/$backup_name" || -L "$INSTALL_BACKUP/$backup_name" ) ]]; then
        cp -a -- "$INSTALL_BACKUP/$backup_name" "$destination"
    else
        rm -f -- "$destination"
    fi
}

cleanup() {
    local status=$?
    trap - EXIT
    if (( status != 0 )); then
        warn "a instalação falhou; restaurando os aplicativos anteriores."
        if [[ "$NORMAL_SWAPPED" == true ]]; then
            rm -rf -- "$NORMAL_APP_DIR"
            if [[ -n "$NORMAL_PREVIOUS_APP" && ( -e "$NORMAL_PREVIOUS_APP" || -L "$NORMAL_PREVIOUS_APP" ) ]]; then
                mv -- "$NORMAL_PREVIOUS_APP" "$NORMAL_APP_DIR"
            fi
        fi
        if [[ "$STUDIO_SWAPPED" == true ]]; then
            rm -rf -- "$STUDIO_APP_DIR"
            if [[ -n "$STUDIO_PREVIOUS_APP" && ( -e "$STUDIO_PREVIOUS_APP" || -L "$STUDIO_PREVIOUS_APP" ) ]]; then
                mv -- "$STUDIO_PREVIOUS_APP" "$STUDIO_APP_DIR"
            fi
        fi
        if [[ -n "$INSTALL_BACKUP" ]]; then
            restore_file "$NORMAL_ID" "$NORMAL_LAUNCHER"
            restore_file "$STUDIO_ID" "$STUDIO_LAUNCHER"
            restore_file "$NORMAL_ID.desktop" "$NORMAL_DESKTOP_FILE"
            restore_file "$STUDIO_ID.desktop" "$STUDIO_DESKTOP_FILE"
        fi
    fi
    for temporary in "$NORMAL_LAUNCHER_TEMP" "$STUDIO_LAUNCHER_TEMP" "$NORMAL_DESKTOP_TEMP" "$STUDIO_DESKTOP_TEMP"; do
        if [[ -n "$temporary" && -e "$temporary" ]]; then rm -f -- "$temporary"; fi
    done
    if [[ -n "$STAGE_ROOT" && -d "$STAGE_ROOT" ]]; then rm -rf -- "$STAGE_ROOT"; fi
    exit "$status"
}
trap cleanup EXIT

version_major() {
    local version="${1#v}"
    [[ "$version" =~ ^([0-9]+) ]] || return 1
    printf '%s\n' "${BASH_REMATCH[1]}"
}

version_minor() {
    local version="${1#v}"
    [[ "$version" =~ ^[0-9]+\.([0-9]+) ]] || return 1
    printf '%s\n' "${BASH_REMATCH[1]}"
}

require_build_tools() {
    local node_version npm_version node_major node_minor npm_major
    command -v node >/dev/null 2>&1 || die "Node.js não foi encontrado. Instale o Node.js 22.12 ou superior."
    command -v npm >/dev/null 2>&1 || die "npm não foi encontrado."
    node_version="$(node --version)"
    npm_version="$(npm --version)"
    node_major="$(version_major "$node_version")" || die "versão do Node.js inválida: $node_version"
    node_minor="$(version_minor "$node_version")" || die "versão do Node.js inválida: $node_version"
    npm_major="$(version_major "$npm_version")" || die "versão do npm inválida: $npm_version"
    (( node_major > MIN_NODE_MAJOR || (node_major == MIN_NODE_MAJOR && node_minor >= MIN_NODE_MINOR) )) || die \
        "Node.js $node_version é incompatível; use a versão 22.12 ou superior."
    (( npm_major >= MIN_NPM_MAJOR )) || die "npm $npm_version é incompatível; use a versão 10 ou superior."
    info "Node.js $node_version e npm $npm_version"
}

require_runtime_tools() {
    command -v kwriteconfig6 >/dev/null 2>&1 || command -v kwriteconfig5 >/dev/null 2>&1 || die "kwriteconfig não foi encontrado."
    command -v kreadconfig6 >/dev/null 2>&1 || command -v kreadconfig5 >/dev/null 2>&1 || die "kreadconfig não foi encontrado."
    command -v qdbus6 >/dev/null 2>&1 || command -v qdbus-qt6 >/dev/null 2>&1 || command -v qdbus >/dev/null 2>&1 || die "QDBus não foi encontrado."
    info "Ferramentas de integração com o KDE encontradas"
}

desktop_exec_escape() {
    local value="$1"
    value="${value//\\/\\\\}"
    value="${value//\"/\\\"}"
    value="${value//\$/\\\$}"
    printf '%s' "$value"
}

verify_binary() {
    local binary="$1"
    [[ -f "$binary" && -x "$binary" ]] || die "o executável esperado não foi criado: $binary"
    if command -v ldd >/dev/null 2>&1; then
        local missing_libraries
        missing_libraries="$(LC_ALL=C ldd "$binary" 2>/dev/null | awk '/not found/{print $1}' || true)"
        if [[ -n "$missing_libraries" ]]; then
            printf '%s\n' "$missing_libraries" >&2
            die "há bibliotecas do sistema ausentes para $binary."
        fi
    fi
}

[[ "$(uname -s)" == "Linux" ]] || die "este instalador é destinado ao Linux/Kubuntu."
[[ -n "$USER_HOME" && -d "$USER_HOME" ]] || die "HOME não aponta para um diretório válido."
[[ "$CONFIG_HOME" == /* ]] || die "XDG_CONFIG_HOME precisa ser um caminho absoluto."
(( EUID != 0 )) || die "não execute este instalador com sudo ou como root."
[[ -f "$SOURCE_DIR/package.json" ]] || die "package.json não encontrado em $SOURCE_DIR."
[[ -x "$SOURCE_DIR/install-kwin-effect.sh" ]] || die "install-kwin-effect.sh não existe ou não é executável."

printf '== %s ==\n' "$SUITE_NAME"
require_build_tools
require_runtime_tools

printf '\nPreparando o efeito global compartilhado do KWin...\n'
"$SOURCE_DIR/install-kwin-effect.sh"

printf '\nPreparando dependências...\n'
cd -- "$SOURCE_DIR"
if [[ -f package-lock.json ]]; then npm ci; else warn "package-lock.json não encontrado; usando npm install."; npm install; fi

printf '\nGerando os dois pacotes Electron independentes...\n'
npm run package:linux

[[ -d "$NORMAL_PACKAGE_OUTPUT" ]] || die "pacote normal não encontrado: $NORMAL_PACKAGE_OUTPUT"
[[ -d "$STUDIO_PACKAGE_OUTPUT" ]] || die "pacote Studio não encontrado: $STUDIO_PACKAGE_OUTPUT"
verify_binary "$NORMAL_PACKAGE_BINARY"
verify_binary "$STUDIO_PACKAGE_BINARY"

mkdir -p -- "$DATA_HOME" "$NORMAL_APP_ROOT" "$STUDIO_APP_ROOT" "$BIN_DIR" "$APPLICATIONS_DIR" \
    "$CONFIG_HOME/$NORMAL_ID" "$CONFIG_HOME/$STUDIO_ID" "$BACKUP_ROOT/installations"
chmod 700 "$CONFIG_HOME/$NORMAL_ID" "$CONFIG_HOME/$STUDIO_ID" "$BACKUP_ROOT" "$BACKUP_ROOT/installations" 2>/dev/null || true

STAGE_ROOT="$(mktemp -d "$DATA_HOME/.radiantcursor-install.XXXXXXXX")"
mkdir -p -- "$STAGE_ROOT/normal" "$STAGE_ROOT/studio"
cp -a -- "$NORMAL_PACKAGE_OUTPUT/." "$STAGE_ROOT/normal/"
cp -a -- "$STUDIO_PACKAGE_OUTPUT/." "$STAGE_ROOT/studio/"
[[ -x "$STAGE_ROOT/normal/$NORMAL_ID" ]] || die "o binário normal perdeu a permissão de execução."
[[ -x "$STAGE_ROOT/studio/$STUDIO_ID" ]] || die "o binário Studio perdeu a permissão de execução."

STAMP="$(date +%Y%m%d-%H%M%S).$$"
INSTALL_BACKUP="$BACKUP_ROOT/installations/$STAMP"
mkdir -p -- "$INSTALL_BACKUP"
if [[ -f "$CONFIG_HOME/kwinrc" ]]; then
    cp -p -- "$CONFIG_HOME/kwinrc" "$BACKUP_ROOT/kwinrc.$STAMP.bak"
    info "Backup do KWin: $BACKUP_ROOT/kwinrc.$STAMP.bak"
fi

for source_and_name in \
    "$NORMAL_LAUNCHER:$NORMAL_ID" "$STUDIO_LAUNCHER:$STUDIO_ID" \
    "$NORMAL_DESKTOP_FILE:$NORMAL_ID.desktop" "$STUDIO_DESKTOP_FILE:$STUDIO_ID.desktop"; do
    source_path="${source_and_name%%:*}"
    backup_name="${source_and_name#*:}"
    if [[ -e "$source_path" || -L "$source_path" ]]; then cp -a -- "$source_path" "$INSTALL_BACKUP/$backup_name"; fi
done

NORMAL_LAUNCHER_TEMP="$(mktemp "$BIN_DIR/.$NORMAL_ID.XXXXXXXX")"
printf '%s\n' '#!/usr/bin/env bash' 'set -euo pipefail' \
    "exec \"$NORMAL_APP_DIR/$NORMAL_ID\" \"\$@\"" > "$NORMAL_LAUNCHER_TEMP"
chmod 0755 "$NORMAL_LAUNCHER_TEMP"

STUDIO_LAUNCHER_TEMP="$(mktemp "$BIN_DIR/.$STUDIO_ID.XXXXXXXX")"
printf '%s\n' '#!/usr/bin/env bash' 'set -euo pipefail' \
    "exec \"$STUDIO_APP_DIR/$STUDIO_ID\" \"\$@\"" > "$STUDIO_LAUNCHER_TEMP"
chmod 0755 "$STUDIO_LAUNCHER_TEMP"

NORMAL_DESKTOP_TEMP="$(mktemp --suffix=.desktop "$APPLICATIONS_DIR/.$NORMAL_ID.XXXXXXXX")"
escaped_normal_launcher="$(desktop_exec_escape "$NORMAL_LAUNCHER")"
printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Version=1.0' \
    'Name=RadiantCursor' 'GenericName=Efeitos de clique e rastro' \
    'Comment=Escolha e execute efeitos de clique no KDE Plasma' \
    "Exec=\"$escaped_normal_launcher\"" "TryExec=$NORMAL_LAUNCHER" \
    "Icon=$NORMAL_APP_DIR/resources/rc-icon.png" 'Terminal=false' \
    'Categories=Settings;DesktopSettings;Utility;' 'Keywords=KDE;KWin;mouse;click;clique;rastro;efeito;' \
    'StartupNotify=true' 'StartupWMClass=radiantcursor' > "$NORMAL_DESKTOP_TEMP"
chmod 0644 "$NORMAL_DESKTOP_TEMP"

STUDIO_DESKTOP_TEMP="$(mktemp --suffix=.desktop "$APPLICATIONS_DIR/.$STUDIO_ID.XXXXXXXX")"
escaped_studio_launcher="$(desktop_exec_escape "$STUDIO_LAUNCHER")"
printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Version=1.0' \
    'Name=RadiantCursor Studio' 'GenericName=Editor de efeitos de cursor' \
    'Comment=Crie e publique efeitos para o RadiantCursor no KDE Plasma' \
    "Exec=\"$escaped_studio_launcher\"" "TryExec=$STUDIO_LAUNCHER" \
    "Icon=$STUDIO_APP_DIR/resources/rcs-icon.png" 'Terminal=false' \
    'Categories=Graphics;Settings;Utility;' 'Keywords=KDE;KWin;mouse;click;clique;efeito;animação;editor;' \
    'StartupNotify=true' 'StartupWMClass=radiantcursor-studio' > "$STUDIO_DESKTOP_TEMP"
chmod 0644 "$STUDIO_DESKTOP_TEMP"

if [[ -e "$NORMAL_APP_DIR" || -L "$NORMAL_APP_DIR" ]]; then
    NORMAL_PREVIOUS_APP="$NORMAL_APP_ROOT/.previous.$STAMP"
    mv -- "$NORMAL_APP_DIR" "$NORMAL_PREVIOUS_APP"
fi
NORMAL_SWAPPED=true
mv -- "$STAGE_ROOT/normal" "$NORMAL_APP_DIR"

if [[ -e "$STUDIO_APP_DIR" || -L "$STUDIO_APP_DIR" ]]; then
    STUDIO_PREVIOUS_APP="$STUDIO_APP_ROOT/.previous.$STAMP"
    mv -- "$STUDIO_APP_DIR" "$STUDIO_PREVIOUS_APP"
fi
STUDIO_SWAPPED=true
mv -- "$STAGE_ROOT/studio" "$STUDIO_APP_DIR"

mv -- "$NORMAL_LAUNCHER_TEMP" "$NORMAL_LAUNCHER"; NORMAL_LAUNCHER_TEMP=""
mv -- "$STUDIO_LAUNCHER_TEMP" "$STUDIO_LAUNCHER"; STUDIO_LAUNCHER_TEMP=""
mv -- "$NORMAL_DESKTOP_TEMP" "$NORMAL_DESKTOP_FILE"; NORMAL_DESKTOP_TEMP=""
mv -- "$STUDIO_DESKTOP_TEMP" "$STUDIO_DESKTOP_FILE"; STUDIO_DESKTOP_TEMP=""

if command -v update-desktop-database >/dev/null 2>&1; then update-desktop-database "$APPLICATIONS_DIR" >/dev/null 2>&1 || true; fi
if command -v kbuildsycoca6 >/dev/null 2>&1; then kbuildsycoca6 --noincremental >/dev/null 2>&1 || true; fi

NORMAL_SWAPPED=false
STUDIO_SWAPPED=false
for previous in "$NORMAL_PREVIOUS_APP" "$STUDIO_PREVIOUS_APP"; do
    if [[ -n "$previous" && ( -e "$previous" || -L "$previous" ) ]]; then rm -rf -- "$previous" || warn "não foi possível remover $previous"; fi
done
if [[ -e "$LEGACY_APP" || -L "$LEGACY_APP" ]]; then rm -f -- "$LEGACY_APP" || warn "não foi possível remover o executável legado."; fi

printf '\nRadiantCursor instalado em: %s\n' "$NORMAL_APP_DIR"
printf 'RadiantCursor Studio instalado em: %s\n' "$STUDIO_APP_DIR"
printf 'Comando normal: %s\n' "$NORMAL_LAUNCHER"
printf 'Comando Studio: %s\n' "$STUDIO_LAUNCHER"
if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then printf 'Observação: adicione %s ao PATH.\n' "$BIN_DIR"; fi
