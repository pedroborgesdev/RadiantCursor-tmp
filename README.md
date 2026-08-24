# RadiantCursor + RadiantCursor Studio

O projeto instala dois aplicativos independentes que compartilham o mesmo formato de efeitos e selecionam automaticamente o runtime da plataforma:

- **RadiantCursor**: executor simples para escolher, personalizar, ativar e desativar os 28 efeitos de clique e 20 rastros clássicos no KWin ou Windows.
- **RadiantCursor Studio**: editor de motion design para criar, salvar e publicar composições geométricas.

As interfaces são Electron + TypeScript + React + Tailwind CSS e o backend também é TypeScript. Não há interface Python ou PyQt.

No Linux, a captura e a renderização são feitas pelo plugin nativo do KWin. No Windows, são feitas pelo processo independente `RadiantCursor.Runtime.exe`, usando hook global Win32, Direct3D 11, Direct2D e DirectComposition. Toda configuração, validação, biblioteca de efeitos e comunicação continuam no backend TypeScript do Electron.

O Studio compila um único `RuntimeDefinition` (`runtime.json`). O plugin KWin e o runtime Windows interpretam esse mesmo arquivo; um projeto não precisa ser convertido ou duplicado por plataforma.

## Motion editor geométrico (schema v2)

O RadiantCursor Studio funciona como um mini editor de motion design. O documento editável é hierárquico e normalizado; o deploy o valida e compila para um `runtime.json` linear e imutável que o plugin carrega uma única vez.

- formas circle, rectangle, triangle, diamond, star, hexagon, polygon e line;
- fill e stroke independentes, inclusive combinados no mesmo elemento;
- canvas SVG com origem do clique, grid, zoom, seleção, movimento, resize e rotação;
- árvore com visibilidade, bloqueio, renomeação, drag-and-drop, grupos e grupos aninhados;
- transformações locais herdadas por `parent × local`;
- timing relativo ao grupo e ação para esticar toda a composição interna;
- timeline hierárquica com scrub, movimento e resize dos blocos; qualquer drag pausa o preview;
- presets convertidos em tracks genéricas: entrada, saída, movimento e transformação;
- várias animações simultâneas por forma ou grupo, com easings independentes;
- undo/redo, copiar/colar, duplicar, apagar, agrupar e desagrupar por atalhos;
- drafts, bundles `.radiantcursor` v2 e revisões por SHA-256 calculadas sobre o runtime canônico;
- limite de 24 instâncias simultâneas no compositor e compartilhamento da definição compilada;
- fallback de leitura para revisões v1 já instaladas e migração de drafts antigos baseados em formas.

Imagens, GIFs, emissores, física e shaders personalizados não fazem parte do schema v2. O código legado continua no plugin apenas como compatibilidade para configurações antigas de clique e rastro.

No Linux, documentos e assets ficam em `~/.local/share/radiantcursor-studio`. No Windows, ficam em `%LOCALAPPDATA%\RadiantCursor`. O `kwinrc` armazena apenas a ativação do Linux; o Windows usa `runtime\state.json` e comunicação local por Named Pipe.

## Recursos legados preservados

- abas independentes para a engine, efeitos de clique e rastro do cursor;
- navegação em módulos independentes para efeitos de clique e rastro do cursor;
- 28 efeitos reais, cada um com ícone próprio, organizados nas páginas de
  contorno e preenchidos: Ondas, Pulso, Alvo,
  Explosão, Faísca, Foco, Halo, Impacto, Órbita, Pétalas, Diamante, Sonar,
  Vórtice, Cruz, Confete, Relâmpago, Bolhas, Coração, Tinta, Splash,
  Supernova, Cometa, Eclipse, Plasma, Pixels, Prisma, Flor cheia e Meteoro;
- disparo ao pressionar, ao soltar ou nos dois momentos;
- brilho opcional renderizado pelo compositor;
- quatro composições determinísticas alternadas, sem repetição consecutiva,
  para efeitos com partículas e elementos dispersos;
- 20 rastros globais personalizáveis: Pontos, Suave, Neon, Cometa, Fumaça,
  Faíscas, Bolhas, Estrelas, Corações, Quadrados, Diamantes, Triângulos, Fita,
  Laser, Fogo, Gelo, Pétalas, Pixels, Órbita e Arco-íris;
- controles independentes de cor, tamanho, duração, densidade, frequência, opacidade,
  brilho e ativação somente durante arraste para o rastro;
- cada emissão de rastro gera múltiplas partículas com variação de tamanho e
  direção, alternando entre quatro distribuições sem repetição consecutiva;
- cores independentes para cada botão;
- controles de tamanho, espessura, duração e quantidade/detalhes;
- texto opcional com família, tamanho, peso e estilo da fonte;
- ações para recarregar, repor os valores na tela, aplicar, aplicar e ativar ou desativar o efeito;
- indicação de alterações ainda não aplicadas e retorno visual das operações.

## Plataformas e requisitos

### Linux/KDE

- Kubuntu ou outra distribuição Linux com KDE Plasma 6 e KWin;
- área mínima de janela de 1100 × 680 px;
- Node.js 22.12 ou superior;
- npm 10 ou superior;
- `kwriteconfig6` ou `kwriteconfig5`;
- `kreadconfig6` ou `kreadconfig5`;
- `qdbus6`, `qdbus-qt6` ou `qdbus`;
- compilador C++, CMake, Extra CMake Modules e cabeçalhos do KWin;
- cabeçalhos do DRM usados pelas dependências públicas do KWin;
- ferramentas e bibliotecas básicas exigidas pelo Electron no Linux.

### Windows

- Windows 10 22H2 ou Windows 11, x64;
- Node.js 22.12 ou superior e npm 10 ou superior para desenvolvimento;
- Visual Studio 2022 Build Tools com o workload **Desktop development with C++**;
- CMake 3.20 ou superior;
- Windows 10/11 SDK com Direct2D, Direct3D 11, DirectComposition e DirectWrite.

O aplicativo instalado não exige Node.js, Visual Studio, Qt, Python nem privilégios administrativos. O runtime roda na sessão do usuário, não como serviço.

## Arquitetura multiplataforma

```text
EffectDocumentV2
       │
       ▼
compilador TypeScript
       │
       ▼
RuntimeDefinition v1
       │
       ├── KWinRuntimeAdapter ──► radiantcursor.so
       └── WindowsRuntimeAdapter ──► RadiantCursor.Runtime.exe
```

Os runtimes nativos ficam separados por plataforma em `native/kwin` e
`native/windows`. Os adaptadores Electron correspondentes ficam em
`src/main/platform/kwin` e `src/main/platform/windows`.

No Windows, o runtime possui:

- uma instância única por sessão;
- hook `WH_MOUSE_LL` somente para eventos dos botões;
- amostragem independente do cursor para respeitar a frequência do rastro;
- uma janela transparente, não ativável e click-through por monitor;
- renderização acelerada com swap chains de composição e alpha premultiplicado;
- suporte a DPI Per-Monitor V2 e reconstrução dos overlays quando os monitores mudam;
- quatro distribuições determinísticas sem repetição consecutiva;
- recarga em tempo real por `\\.\pipe\LOCAL\RadiantCursor.Runtime`;
- inicialização automática enquanto um efeito estiver ativado;
- logs em `%LOCALAPPDATA%\RadiantCursor\logs\runtime.log`.

Node.js e npm são usados para gerar o pacote local. O aplicativo instalado inclui o runtime do Electron e não depende deles para ser iniciado.

No Kubuntu/Ubuntu, as dependências de compilação do efeito podem ser instaladas com:

```bash
sudo apt update
sudo apt install -y cmake extra-cmake-modules kwin-dev libdrm-dev
```

O instalador não instala pacotes do sistema automaticamente. Ele usa `sudo` apenas para copiar o plugin compilado para o diretório global de plugins do KWin; o aplicativo Electron continua instalado apenas para o usuário atual.

## Instalação no Kubuntu

Extraia ou clone o projeto, entre na pasta e execute:

```bash
chmod +x install.sh install-kwin-effect.sh uninstall.sh
./install.sh
```

Não execute o script com `sudo`. A instalação pertence ao usuário atual e usa estes caminhos:

```text
~/.local/share/radiantcursor/app
~/.local/share/radiantcursor-studio/app
~/.local/bin/radiantcursor
~/.local/bin/radiantcursor-studio
~/.local/share/applications/radiantcursor-studio.desktop
~/.local/share/applications/radiantcursor.desktop
```

O script compila o plugin compartilhado contra a versão do KWin instalada, instala esse binário com uma chamada pontual ao `sudo`, valida Node.js e npm, instala as dependências com `npm ci` e gera dois bundles Electron independentes. Cada aplicação possui executável, appId, interface, ícone, diretório de instalação e dados de usuário próprios.

Depois, procure por **RadiantCursor** ou **RadiantCursor Studio** no menu de aplicativos. Pelo terminal, use:

```bash
radiantcursor
radiantcursor-studio
```

Se o shell não encontrar os comandos, inclua `~/.local/bin` no `PATH` ou execute os caminhos completos:

```bash
~/.local/bin/radiantcursor
~/.local/bin/radiantcursor-studio
```

## Desenvolvimento

Instale as dependências e abra o aplicativo desejado:

```bash
npm install
npm run dev:radiantcursor
npm run dev:studio
```

`npm run dev` continua sendo um atalho para o RadiantCursor normal.

Comandos de build e empacotamento da interface:

```bash
npm run build
npm test
npm run package:linux
```

No Windows, abra um terminal de desenvolvedor do Visual Studio e use:

```powershell
npm ci
npm run build:runtime:windows
npm run dev:radiantcursor
# ou
npm run dev:studio
```

Para gerar os dois instaladores independentes:

```powershell
npm run package:windows
```

Os instaladores são escritos em `release/radiantcursor-windows` e `release/radiantcursor-studio-windows`. Os dois incluem o runtime nativo para que Normal e Studio possam ser instalados separadamente.

O núcleo portátil do interpretador pode ser testado também no Linux:

```bash
npm run test:native-runtime
```

O backend compartilhado é gerado em `dist/main`. As interfaces independentes ficam em `dist/renderer-normal` e `dist/renderer-studio`. Os pacotes Linux ficam em `release/radiantcursor/linux-unpacked` e `release/radiantcursor-studio/linux-unpacked`.

Para compilar e instalar somente o efeito do KWin:

```bash
./install-kwin-effect.sh
```

Plugins nativos do KWin não têm estabilidade binária entre versões. Depois de atualizar o KWin, execute novamente esse script para recompilar o efeito contra os novos cabeçalhos.

Para validar também o plugin nativo sem instalá-lo:

```bash
cmake -S native/kwin -B /tmp/radiantcursor-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/radiantcursor-build --parallel
```

Se o Electron informar que o KWin recusou `loadEffect`, reinstale somente o
plugin e reinicie apenas o compositor; não é necessário encerrar a sessão:

```bash
./install-kwin-effect.sh
kwin_wayland --replace >/tmp/radiantcursor-kwin.log 2>&1 & disown
```

Depois de alguns segundos, confirme que o KWin reconheceu o efeito:

```bash
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.isEffectSupported radiantcursor
```

O resultado esperado é `true`. Mensagens do Electron sobre
`wayland_wp_color_manager` não impedem o RadiantCursor de funcionar; elas são avisos
do Chromium sobre gerenciamento de cor.

## Integração com o KWin

As preferências são lidas e gravadas no grupo `[Effect-radiantcursor]` de `~/.config/kwinrc`. `ActiveEffectId` e `ActiveRevision` apontam para a revisão declarativa. As opções `Color1`, `Color2`, `Color3`, `LineWidth`, `RingLife`, `RingSize`, `RingCount`, `ShowText`, `Font`, `Style`, `Trigger`, `Glow` e as opções de rastro continuam disponíveis para compatibilidade. A ativação é registrada em `Plugins/radiantcursorEnabled`.

Depois de aplicar uma alteração, o backend solicita ao KWin que recarregue a configuração. A ação **Aplicar e ativar** carrega o plugin RadiantCursor e desabilita o `mouseclick` padrão para evitar efeitos duplicados; **Desativar** descarrega o RadiantCursor. Toda integração com o sistema é feita pelo processo principal do Electron, e a interface React não recebe acesso direto ao Node.js.

## Integração com o Windows

O backend grava configurações validadas de forma atômica e envia apenas comandos limitados (`PING`, `RELOAD` e `STOP`) ao runtime. O processo nativo não injeta DLLs, não bloqueia os eventos do mouse e não precisa executar como administrador.

O overlay não aparece na tela segura do UAC ou na tela de login. Aplicações em fullscreen exclusivo também podem ficar acima dele; fullscreen sem borda e o desktop composto pelo DWM são suportados.

## Backups e atualização

Antes de instalar, o script salva uma cópia de `~/.config/kwinrc`, quando o arquivo existe. Em uma atualização, também guarda o bundle, o launcher e a entrada de menu anteriores. Os backups ficam em:

```text
~/.config/radiantcursor-studio/backups
```

O backend também cria um backup por sessão antes da primeira gravação. O estado local do Electron e os backups permanecem dentro de `~/.config/radiantcursor-studio` por padrão. Se `XDG_CONFIG_HOME` estiver definido com um caminho absoluto, essa pasta é criada dentro dele. A troca do bundle é preparada em uma pasta temporária; se ela falhar durante a atualização, o instalador tenta restaurar a versão anterior.

## Desinstalação

Na pasta do projeto, execute:

```bash
./uninstall.sh
```

O script remove o bundle instalado, o launcher, a entrada do menu e, após solicitar `sudo`, o binário global `radiantcursor.so`. Ele não apaga:

- os backups em `~/.config/radiantcursor-studio/backups`;
- outras configurações em `~/.config/radiantcursor-studio`;
- as preferências já gravadas em `~/.config/kwinrc`.

Isso evita perder uma personalização do KWin ou uma versão anterior sem uma ação explícita do usuário.
