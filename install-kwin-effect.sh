#!/usr/bin/env bash
set -Eeuo pipefail

readonly EFFECT_ID="radiantcursor"
readonly SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly EFFECT_SOURCE="$SOURCE_DIR/native/kwin"

BUILD_DIR=""
QDBUS_COMMAND=""
EFFECT_WAS_LOADED="false"
PLASMA_VERSION=""

die() {
    printf 'Erro: %s\n' "$*" >&2
    exit 1
}

cleanup() {
    local status=$?
    trap - EXIT
    if [[ -n "$BUILD_DIR" && -d "$BUILD_DIR" ]]; then
        rm -rf -- "$BUILD_DIR"
    fi
    exit "$status"
}
trap cleanup EXIT

(( EUID != 0 )) || die "execute este script como usuário normal; ele solicitará sudo apenas para instalar o plugin."
[[ -f "$EFFECT_SOURCE/CMakeLists.txt" ]] || die "fontes do efeito não encontradas em $EFFECT_SOURCE."

for command_name in cmake c++ qtpaths6 qtplugininfo6 readelf nm; do
    command -v "$command_name" >/dev/null 2>&1 || die "comando ausente: $command_name."
done

if command -v plasmashell >/dev/null 2>&1; then
    PLASMA_VERSION="$(plasmashell --version 2>/dev/null | awk '{print $NF}' | head -n1)"
fi
[[ "$PLASMA_VERSION" =~ ^6\. ]] || die \
    "esta versão do RadiantCursor requer KDE Plasma 6; versão detectada: ${PLASMA_VERSION:-desconhecida}."

for qdbus_candidate in qdbus6 qdbus-qt6 qdbus; do
    if command -v "$qdbus_candidate" >/dev/null 2>&1; then
        QDBUS_COMMAND="$qdbus_candidate"
        break
    fi
done

[[ -f /usr/lib/x86_64-linux-gnu/cmake/KWin/KWinConfig.cmake || -f /usr/lib/aarch64-linux-gnu/cmake/KWin/KWinConfig.cmake ]] || die \
    "kwin-dev não foi localizado. Instale cmake, extra-cmake-modules e kwin-dev."
[[ -f /usr/include/libdrm/drm.h ]] || die \
    "libdrm-dev não foi localizado. Execute: sudo apt install -y libdrm-dev"

PLUGIN_ROOT="$(qtpaths6 --plugin-dir 2>/dev/null)" || die "não foi possível descobrir o diretório de plugins do Qt 6."
[[ "$PLUGIN_ROOT" == /* && -d "$PLUGIN_ROOT" ]] || die "diretório de plugins Qt inválido: $PLUGIN_ROOT"
PLUGIN_DIR="$PLUGIN_ROOT/kwin/effects/plugins"
PLUGIN_FILE="$PLUGIN_DIR/$EFFECT_ID.so"

BUILD_DIR="$(mktemp -d /tmp/radiantcursor-effect-build.XXXXXXXX)"

printf '== Plugin RadiantCursor para KWin ==\n'
printf 'KDE Plasma %s detectado. O plugin será compilado contra o KWin instalado.\n' "$PLASMA_VERSION"
printf 'Compilando contra a versão instalada do KWin...\n'
cmake -S "$EFFECT_SOURCE" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD_DIR" --parallel

BUILT_PLUGIN="$(find "$BUILD_DIR" -type f -name "$EFFECT_ID.so" -print -quit)"
[[ -n "$BUILT_PLUGIN" && -f "$BUILT_PLUGIN" ]] || die "a compilação não gerou $EFFECT_ID.so."

# A KWin effect must be a discoverable Qt plugin, not only a valid shared
# library. This catches an easy-to-miss AUTOMOC regression before sudo copies
# a binary that KWin will always reject from loadEffect().
PLUGIN_SECTIONS="$(readelf -SW "$BUILT_PLUGIN")"
PLUGIN_SYMBOLS="$(nm -D "$BUILT_PLUGIN")"
PLUGIN_INFO="$(qtplugininfo6 "$BUILT_PLUGIN" 2>/dev/null)" || die \
    "o binário compilado não é reconhecido como plugin pelo Qt."
grep -q '\.note\.qt\.metadata' <<< "$PLUGIN_SECTIONS" || die \
    "o plugin compilado não contém metadados Qt; verifique KWIN_EFFECT_FACTORY e a inclusão de main.moc."
grep -q 'qt_plugin_instance' <<< "$PLUGIN_SYMBOLS" || die \
    "o plugin compilado não exporta a factory Qt esperada pelo KWin."
grep -q 'org\.kde\.kwin\.EffectPluginFactory' <<< "$PLUGIN_INFO" || die \
    "o plugin compilado não expõe a interface de efeito esperada pelo KWin."

if [[ -n "$QDBUS_COMMAND" ]]; then
    if [[ "$($QDBUS_COMMAND org.kde.KWin /Effects org.kde.kwin.Effects.isEffectLoaded "$EFFECT_ID" 2>/dev/null || true)" == "true" ]]; then
        EFFECT_WAS_LOADED="true"
        printf 'Descarregando temporariamente o efeito ativo...\n'
        "$QDBUS_COMMAND" org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect "$EFFECT_ID" >/dev/null || \
            die "o KWin não permitiu descarregar o efeito ativo."
    fi
fi

if [[ -f "$PLUGIN_FILE" ]]; then
    BACKUP_FILE="$PLUGIN_FILE.bak.$(date +%Y%m%d-%H%M%S)"
    printf 'Salvando a versão anterior em %s\n' "$BACKUP_FILE"
    sudo cp -a -- "$PLUGIN_FILE" "$BACKUP_FILE"
fi

printf 'Instalando em %s (o sudo será solicitado)...\n' "$PLUGIN_FILE"
if ! sudo install -D -m 0755 -- "$BUILT_PLUGIN" "$PLUGIN_FILE"; then
    if [[ "$EFFECT_WAS_LOADED" == "true" ]]; then
        "$QDBUS_COMMAND" org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect "$EFFECT_ID" >/dev/null 2>&1 || true
    fi
    die "não foi possível instalar o novo plugin."
fi

if [[ "$EFFECT_WAS_LOADED" == "true" ]]; then
    printf 'Carregando a nova versão no KWin...\n'
    "$QDBUS_COMMAND" org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect "$EFFECT_ID" >/dev/null || \
        die "o plugin foi instalado, mas o KWin não conseguiu recarregá-lo. Abra o app e clique em Aplicar e ativar."
    "$QDBUS_COMMAND" org.kde.KWin /Effects org.kde.kwin.Effects.reconfigureEffect "$EFFECT_ID" >/dev/null 2>&1 || true
fi

if [[ -n "$QDBUS_COMMAND" ]]; then
    DISCOVERED="$($QDBUS_COMMAND org.kde.KWin /Effects org.kde.kwin.Effects.isEffectSupported "$EFFECT_ID" 2>/dev/null || true)"
    if [[ "$DISCOVERED" != "true" ]]; then
        printf '\nAviso: o KWin ainda mantém em memória a lista anterior de plugins.\n' >&2
        printf 'Reinicie somente o compositor (não exige logout) e abra o app novamente:\n' >&2
        printf '  kwin_wayland --replace >/tmp/radiantcursor-kwin.log 2>&1 & disown\n' >&2
    fi
fi

printf 'Plugin RadiantCursor instalado%s. Nenhum logout é necessário.\n' \
    "$([[ "$EFFECT_WAS_LOADED" == "true" ]] && printf ' e recarregado' || true)"
