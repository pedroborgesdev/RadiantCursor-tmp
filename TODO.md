# Implementação Motion v2

As fases abaixo foram implementadas na reformulação do editor:

- [x] schema hierárquico v2 de formas e grupos;
- [x] transformações locais/globais e timing relativo;
- [x] compilador de presets para tracks/keyframes genéricas;
- [x] validação, limites e migração de drafts v1;
- [x] repositório com `document.json`, `runtime.json` e revisão imutável;
- [x] canvas SVG visual com drag-and-drop, seleção, resize e rotação;
- [x] árvore aninhada, grupos, reordenação, visibilidade e bloqueio;
- [x] timeline hierárquica arrastável com pausa automática;
- [x] painel de animações combináveis e easings;
- [x] undo/redo, clipboard e atalhos do editor;
- [x] carregador/evaluador C++ v2 com fallback v1;
- [x] limite de instâncias simultâneas e definição compartilhada;
- [x] testes TypeScript, build Electron e build nativo.

---

# Análise arquitetural original

É viável transformar o RadiantCursor Studio em uma mini-engine de VFX declarativa. A melhor solução é manter o plugin C++ responsável por captura, compilação e renderização, enquanto o Electron administra documentos, assets, edição, validação e distribuição.

A principal decisão é: o JSON não deve ser interpretado durante o clique nem durante cada frame. Ele deve ser carregado uma vez, validado, compilado para estruturas C++ eficientes e mantido em memória.

```text
React Editor
    ↓ EffectDocument
Validação TypeScript + importação de assets
    ↓ bundle imutável
Effect Repository
    ↓ reconfigureEffect
Parser e validador C++
    ↓
CompiledEffect + recursos preparados
    ↓
RuntimeInstance por clique
    ↓
RenderQueue agrupada
    ↓
OpenGL / QPainter
```

Nenhum arquivo foi alterado nesta análise.

# Estado atual

## Editor React

A interface está concentrada principalmente em [App.tsx](/home/pedroborges/Code/src/renderer/App.tsx:194). Os 28 efeitos e 20 rastros são catálogos estáticos definidos diretamente no arquivo, em [App.tsx](/home/pedroborges/Code/src/renderer/App.tsx:67) e [App.tsx](/home/pedroborges/Code/src/renderer/App.tsx:104).

O editor atual trabalha com um único objeto plano `RadiantCursorSettings`. A comparação manual de todos os campos está em [App.tsx](/home/pedroborges/Code/src/renderer/App.tsx:153).

Isso funciona para configurações fixas, mas não escala para:

- Quantidade variável de layers.
- Keyframes.
- Assets.
- Timeline.
- Undo/redo.
- Presets e revisões.
- Documentos com schemas diferentes.
- Seleção e edição de layers.

A futura engine precisa separar:

```text
AppSettings        preferências gerais
EffectDocument     efeito que está sendo editado
EditorState        seleção, timeline, zoom, histórico
DeploymentState    revisão aplicada no KWin
RuntimeStatus      resultado da compilação nativa
```

## Electron e IPC

A segurança atual é uma boa base:

- `contextIsolation` habilitado.
- `nodeIntegration` desabilitado.
- Renderer sandboxed.
- Navegação externa bloqueada.
- IPC exposto por uma API pequena.

Isso aparece em [main.ts](/home/pedroborges/Code/src/main/main.ts:171) e [preload.ts](/home/pedroborges/Code/src/main/preload.ts:10).

O processo principal também revalida os dados recebidos do renderer antes de tocar no KWin, em [kwin-adapter.ts](/home/pedroborges/Code/src/main/platform/kwin/kwin-adapter.ts:180). Essa fronteira deve ser preservada.

O problema é que a API atual só entende um objeto fixo de configurações:

```ts
getState()
applySettings()
activateEffect()
disableEffect()
```

Ela precisará evoluir para operações de documentos e assets.

## Persistência atual

Cada configuração é gravada individualmente em `kwinrc`, através de processos `kwriteconfig`, em [kwin-adapter.ts](/home/pedroborges/Code/src/main/platform/kwin/kwin-adapter.ts:506).

Isso não deve ser usado para armazenar uma engine inteira. JSON, keyframes e referências de assets seriam grandes demais e difíceis de atualizar atomicamente.

O `kwinrc` deve guardar somente algo como:

```ini
[Effect-radiantcursor]
ActiveEffectId=...
ActiveRevision=...
ClickEnabled=true
TrailEnabled=false
```

O documento completo deve ficar em arquivos próprios.

## Captura dos cliques

O plugin conecta-se ao sinal global `EffectsHandler::mouseChanged` durante sua criação, em [radiantcursoreffect.cpp](/home/pedroborges/Code/native/kwin/radiantcursoreffect.cpp:65).

A cada evento:

1. Compara `buttons` e `oldButtons`.
2. Detecta press ou release.
3. Determina o botão.
4. Cria um `ClickEvent`.
5. Armazena o evento em uma fila.

Isso está em [radiantcursoreffect.cpp](/home/pedroborges/Code/native/kwin/radiantcursoreffect.cpp:221).

Esse mecanismo já fornece quase todo o `ClickContext` inicial:

```cpp
struct ClickContext {
    QPointF position;
    MouseButton button;
    bool pressed;
    uint64_t timestamp;
    uint64_t sequence;
    int variation;
};
```

Modificadores já chegam no callback, embora ainda sejam ignorados. Tela, escala e geometria podem ser resolvidas a partir da posição e do contexto do KWin.

## Renderização atual

O ciclo atual é:

- `prePaintScreen`: avança o tempo e remove eventos expirados.
- `paintScreen`: configura shader/blending e desenha rastros e cliques.
- `postPaintScreen`: solicita repaint das regiões afetadas.

Isso está em [radiantcursoreffect.cpp](/home/pedroborges/Code/native/kwin/radiantcursoreffect.cpp:140).

O problema central é o dispatch por estilo:

```cpp
if (style == "pulse") drawPulse();
else if (style == "target") drawTarget();
else if (...) ...
```

Ele aparece em [radiantcursoreffect.cpp](/home/pedroborges/Code/native/kwin/radiantcursoreffect.cpp:422). As funções específicas também estão declaradas diretamente na classe principal, em [radiantcursoreffect.h](/home/pedroborges/Code/native/kwin/radiantcursoreffect.h:72).

Hoje, adicionar um efeito significa:

- Alterar os tipos TypeScript.
- Alterar o validador.
- Alterar o catálogo React.
- Adicionar uma função C++.
- Adicionar o dispatch.
- Recompilar o plugin.

É exatamente o acoplamento que a engine precisa eliminar.

# Refatorações necessárias

Os principais pontos são:

1. Separar a captura de input do runtime visual.
2. Substituir estilos C++ específicos por layers compiladas.
3. Separar documento editável de configuração global.
4. Criar um repositório próprio de efeitos e assets.
5. Criar validação equivalente em TypeScript e C++.
6. Compilar JSON para estruturas C++ imutáveis.
7. Criar pools para instâncias, partículas e comandos.
8. Substituir renderização imediata por uma fila agrupada.
9. Criar caches de meshes, texturas e shaders.
10. Manter o efeito anterior caso uma nova revisão falhe.
11. Remover uploads GPU e alocações caras do caminho de clique/frame.

Há um gargalo importante hoje: cada círculo recria 72 vértices, e `drawLines` cria listas, escala os vértices e reinicializa o VBO para cada primitiva, em [radiantcursoreffect.cpp](/home/pedroborges/Code/native/kwin/radiantcursoreffect.cpp:1003) e [radiantcursoreffect.cpp](/home/pedroborges/Code/native/kwin/radiantcursoreffect.cpp:1021).

Além disso, o texto cria `QImage` e textura por evento na primeira renderização, em [radiantcursoreffect.cpp](/home/pedroborges/Code/native/kwin/radiantcursoreffect.cpp:1072). Isso precisará virar cache por texto/fonte.

# Alternativas principais

## Runtime

| Abordagem | Vantagens | Desvantagens | Risco no KWin |
|---|---|---|---|
| C++ declarativo compilado | Melhor performance, controle de memória, assets e batching | Implementação inicial maior | Baixo após estabilização |
| QML/JavaScript | Desenvolvimento mais rápido e distribuição simples | Mais objetos/runtime, menor controle e diferenças de API | Médio |
| Gerar C++ ou shaders por efeito | Liberdade extrema | Compilação, segurança e travamentos | Muito alto |

Recomendação: C++ declarativo compilado.

Shaders arbitrários enviados pelo usuário não devem entrar na primeira versão. Um shader inválido ou excessivamente caro executaria dentro do compositor. A engine deve expor apenas shaders internos conhecidos.

## Comunicação com o plugin

### Opção A — JSON no `kwinrc`

- Simples.
- Não suporta bem assets.
- Tamanho e escaping problemáticos.
- Atualização não é realmente transacional.

Não recomendada.

### Opção B — Bundle imutável + revisão no `kwinrc`

- Assets relativos.
- Escrita atômica.
- Fácil rollback.
- Fácil exportação.
- Plugin carrega somente ao reconfigurar.

Recomendada.

### Opção C — Enviar o documento inteiro via D-Bus

- Atualização rápida.
- Feedback direto.
- Mais complexidade e grandes payloads.
- Não resolve armazenamento e assets.

Recomendação final: abordagem híbrida. O bundle fica no disco; D-Bus transmite apenas “recarregue a revisão X” e consulta status/erros.

## Preview

### Canvas/WebGL TypeScript

Boa velocidade de desenvolvimento, integração natural com React e timeline. Pode ter pequenas diferenças visuais do C++.

### Preview diretamente no KWin

Representação exata, mas cada edição afetaria o compositor. Não é aceitável para um editor ainda em construção.

### Runtime C++ compartilhado por WASM

Alta fidelidade, mas aumenta bastante build, manutenção e compatibilidade.

Recomendação: preview Canvas/WebGL com fixtures de conformidade. WASM pode ser avaliado depois.

# Schema recomendado

Eu não criaria `ring` como implementação nativa separada. No editor ele pode aparecer como “Ring Layer”, mas no schema deve ser uma `shape` com geometria `ring`. Isso reduz a quantidade de tipos fundamentais.

Também recomendo keyframes inline e tipados. O compilador C++ os transforma em tracks planas.

```json
{
  "schemaVersion": 1,
  "engine": {
    "minimumVersion": 1,
    "requiredCapabilities": [
      "shape.v1",
      "particles.v1",
      "image.v1"
    ]
  },
  "id": "4f660fcc-21ec-49e8-a439-a78119f44f14",
  "revision": "sha256:...",
  "metadata": {
    "name": "Explosão dupla",
    "author": "Pedro",
    "tags": ["burst", "particles"]
  },
  "durationMs": 650,
  "layers": [
    {
      "id": "main-ring",
      "type": "shape",
      "enabled": true,
      "timing": {
        "startMs": 0,
        "durationMs": 500
      },
      "geometry": {
        "kind": "ring",
        "radius": 54
      },
      "transform": {
        "position": [0, 0],
        "anchor": [0.5, 0.5],
        "scale": {
          "keyframes": [
            {
              "time": 0,
              "value": [0.1, 0.1],
              "easing": "easeOutCubic"
            },
            {
              "time": 1,
              "value": [1, 1]
            }
          ]
        },
        "rotationDeg": 0
      },
      "material": {
        "fill": null,
        "stroke": {
          "color": {
            "ref": "click.buttonColor"
          },
          "width": 3
        },
        "opacity": {
          "keyframes": [
            { "time": 0, "value": 1 },
            { "time": 1, "value": 0 }
          ]
        },
        "blendMode": "normal"
      }
    },
    {
      "id": "particles",
      "type": "particles",
      "timing": {
        "startMs": 80,
        "durationMs": 450
      },
      "emitter": {
        "mode": "radial",
        "count": 12,
        "distribution": "even",
        "speed": 90,
        "speedVariation": 20,
        "spawnRadius": 4,
        "gravity": [0, 0],
        "drag": 0.12,
        "variants": {
          "count": 4,
          "avoidImmediateRepeat": true
        }
      },
      "particle": {
        "geometry": {
          "kind": "diamond",
          "size": 8
        },
        "material": {
          "fill": {
            "color": {
              "ref": "click.buttonColor"
            }
          },
          "opacity": {
            "keyframes": [
              { "time": 0, "value": 0.9 },
              { "time": 1, "value": 0 }
            ]
          }
        },
        "scale": {
          "keyframes": [
            { "time": 0, "value": 0.3 },
            { "time": 1, "value": 1 }
          ]
        }
      }
    },
    {
      "id": "image",
      "type": "image",
      "timing": {
        "startMs": 100,
        "durationMs": 550
      },
      "asset": {
        "ref": "click.buttonImage"
      },
      "transform": {
        "scale": {
          "keyframes": [
            {
              "time": 0,
              "value": [0.4, 0.4],
              "easing": "easeOutBack"
            },
            {
              "time": 1,
              "value": [1.2, 1.2]
            }
          ]
        },
        "rotationDeg": {
          "keyframes": [
            { "time": 0, "value": 0 },
            { "time": 1, "value": 20 }
          ]
        }
      },
      "material": {
        "opacity": {
          "keyframes": [
            { "time": 0, "value": 1 },
            { "time": 1, "value": 0 }
          ]
        },
        "blendMode": "normal"
      }
    }
  ]
}
```

Decisões importantes:

- `time` dos keyframes fica normalizado entre `0` e `1`.
- Tempo absoluto pertence à layer.
- `startMs` substitui a combinação ambígua de `startTime` e `delay`.
- Propriedades constantes usam diretamente o valor.
- Propriedades animadas usam `{ keyframes: [...] }`.
- Referências de contexto são objetos, não strings mágicas como `"$buttonColor"`.
- Nenhuma expressão JavaScript arbitrária é permitida.
- A duração total pode ser calculada pelo compilador, mas pode ser armazenada para leitura rápida.

# Tipos TypeScript

O modelo principal deveria ser um discriminated union:

```ts
type LayerDocument =
  | ShapeLayerDocument
  | ParticleLayerDocument
  | ImageLayerDocument;

type Animatable<T> =
  | T
  | {
      keyframes: Array<{
        time: number;
        value: T;
        easing?: EasingName;
      }>;
    };

type ContextRef =
  | { ref: "click.buttonColor" }
  | { ref: "click.buttonImage" }
  | { ref: "click.position" };

interface BaseLayerDocument {
  id: string;
  name: string;
  enabled: boolean;
  timing: {
    startMs: number;
    durationMs: number;
  };
  transform: TransformDocument;
}

interface ShapeLayerDocument extends BaseLayerDocument {
  type: "shape";
  geometry: ShapeGeometry;
  material: ShapeMaterial;
}

interface ParticleLayerDocument extends BaseLayerDocument {
  type: "particles";
  emitter: ParticleEmitterDocument;
  particle: ParticleTemplateDocument;
}

interface ImageLayerDocument extends BaseLayerDocument {
  type: "image";
  asset: AssetValue;
  material: ImageMaterial;
}

interface EffectDocument {
  schemaVersion: number;
  engine: EngineRequirements;
  id: string;
  revision?: string;
  metadata: EffectMetadata;
  durationMs?: number;
  layers: LayerDocument[];
}
```

O estado do editor não deve fazer parte desse documento:

```ts
interface EffectEditorState {
  selectedLayerIds: string[];
  playheadMs: number;
  playing: boolean;
  zoom: number;
  history: UndoEntry[];
  dirty: boolean;
}
```

# Classes C++ propostas

## Carregamento e compilação

```cpp
class EffectRepository;
class EffectBundleLoader;
class EffectSchemaValidator;
class EffectCompiler;
class CapabilityRegistry;
class AssetResolver;
```

Responsabilidades:

- Resolver efeito e revisão.
- Validar manifest e caminhos.
- Validar limites.
- Migrar ou recusar schemas.
- Converter JSON em estruturas eficientes.
- Resolver assets por hash.
- Preservar o último programa válido.

## Representação compilada

```cpp
struct CompiledEffect;
struct CompiledLayer;
struct CompiledShapeLayer;
struct CompiledParticleLayer;
struct CompiledImageLayer;
struct CompiledTrack;
struct CompiledMaterial;
struct CompiledEmitter;
```

Essas estruturas devem:

- Ser imutáveis.
- Usar enums, não strings.
- Ter vetores reservados.
- Ter propriedades resolvidas por índice.
- Manter handles de assets e meshes.
- Conhecer bounds máximos para repaint e culling.

## Runtime

```cpp
struct ClickContext;
class EffectInstance;
class RuntimeInstancePool;
class ParticlePool;
class AnimationEvaluator;
class EasingEvaluator;
class BudgetManager;
```

Cada clique pega um slot do pool:

```cpp
EffectInstance {
    CompiledEffect* program;
    ClickContext context;
    TimePoint startedAt;
    InstanceState state;
    ParticleRange particles;
}
```

Uma revisão antiga do efeito deve permanecer viva enquanto existirem instâncias usando-a. `shared_ptr<const CompiledEffect>` ou um sistema equivalente resolve isso.

## Renderer

```cpp
class RenderQueue;
class ShapeRenderer;
class ImageRenderer;
class ParticleRenderer;
class MeshCache;
class TextureCache;
class ShaderCache;
```

Adicionar uma nova combinação de shapes e layers não exigiria alterar `RadiantCursorEffect`. Apenas um novo tipo fundamental exigiria registrar um compiler e renderer no `CapabilityRegistry`.

# Comunicação editor–plugin

A API Electron deveria evoluir para algo semelhante a:

```ts
interface RadiantCursorApi {
  listEffects(): Promise<EffectSummary[]>;
  loadEffect(id: string): Promise<EffectDocument>;
  saveDraft(document: EffectDocument): Promise<SaveResult>;
  importAsset(request: ImportAssetRequest): Promise<AssetRecord>;
  removeAsset(assetId: string): Promise<void>;
  deployEffect(document: EffectDocument): Promise<DeployResult>;
  activateRevision(effectId: string, revision: string): Promise<void>;
  exportEffect(effectId: string): Promise<string>;
  importEffect(path: string): Promise<ImportResult>;
  getRuntimeStatus(): Promise<RuntimeStatus>;
}
```

Fluxo de deploy recomendado:

1. React envia o documento ao processo principal.
2. TypeScript valida e normaliza.
3. Assets são verificados por hash.
4. Uma revisão imutável é escrita em diretório temporário.
5. `fsync` e rename atômico publicam a revisão.
6. `kwinrc` recebe somente ID e revisão.
7. `reconfigureEffect("radiantcursor")` é chamado.
8. O plugin valida e compila novamente.
9. Se falhar, mantém o programa anterior.
10. O editor recebe diagnóstico com layer e propriedade responsáveis.

# Assets

Local recomendado:

```text
~/.local/share/radiantcursor-studio/
├── library/
│   └── effects/
│       └── <effect-id>/
│           ├── metadata.json
│           ├── current.json
│           └── revisions/
│               └── <revision-hash>/
│                   ├── effect.json
│                   └── manifest.json
├── assets/
│   └── sha256/
│       ├── abcd....png
│       └── ef01....webp
└── cache/
    ├── thumbnails/
    └── imports/
```

O JSON não deve aceitar caminhos absolutos. Uma imagem deve ser referenciada por ID/hash:

```json
{
  "assetId": "sha256:abc123...",
  "mediaType": "image/png"
}
```

Na exportação `.radiantcursor`, os assets necessários são copiados para dentro do ZIP com caminhos relativos.

## Formatos

Para a primeira versão:

- PNG.
- JPEG.
- WebP estático.
- SVG rasterizado durante a importação.

SVG não confiável deve ser rasterizado fora do KWin. Isso reduz complexidade e superfície de ataque.

GIF, WebP animado e sequências devem ser convertidos durante a importação para:

- Sprite sheet.
- Atlas de frames.
- Manifest de tempos.

O plugin não deve decodificar GIF ou SVG durante o clique.

# Cache

## EffectCache

Chave:

```text
effectId + revisionHash + engineVersion
```

Mantém programas compilados imutáveis e permite rollback.

## TextureCache

Chave:

```text
assetHash + decodedSize + colorSpace
```

Políticas:

- LRU por memória.
- Referências fortes enquanto uma instância estiver ativa.
- Upload fora do clique.
- Invalidação quando o contexto OpenGL mudar.
- Contabilização por bytes reais decodificados, não tamanho do arquivo.

## MeshCache

Meshes unitários reutilizáveis:

- Círculo.
- Anel.
- Quadrado.
- Triângulo.
- Estrela.
- Coração.
- Polígono por quantidade de lados.

Escala, rotação e posição devem ser feitas por matriz, evitando recriar vértices.

## ShaderCache

Somente combinações internas conhecidas:

```text
solid
textured
solid + glow
textured + opacity
additive
```

Nenhum shader fornecido pelo efeito na versão inicial.

# Runtime e performance

O clique deve executar apenas:

1. Obter um slot livre.
2. Copiar um `ClickContext` pequeno.
3. Resolver referências por botão.
4. Gerar seeds/variantes.
5. Reservar partículas em um pool.
6. Marcar repaint.

O clique não pode:

- Abrir arquivos.
- Interpretar JSON.
- Decodificar imagens.
- Compilar shaders.
- Fazer upload GPU.
- Criar meshes.
- Executar regex.
- Alocar grandes vetores.

Durante cada frame:

1. Avançar instâncias.
2. Avaliar tracks compiladas.
3. Atualizar partículas.
4. Fazer culling por output.
5. Gerar comandos em buffer reservado.
6. Ordenar/agrupar por pipeline, blend e textura.
7. Enviar um ou poucos buffers à GPU.

## Renderer recomendado

| Renderer | Complexidade | Performance | Recomendação |
|---|---:|---:|---|
| Imediato, como hoje | Baixa | Baixa com muitos elementos | Apenas durante migração |
| CPU + command buffer batched | Média/alta | Alta e previsível | Recomendado |
| Simulação de partículas totalmente na GPU | Muito alta | Excelente em volumes enormes | Futuro |

A segunda opção oferece o melhor equilíbrio para centenas ou poucos milhares de partículas.

## Múltiplos cliques

Sugestão:

```text
maxActiveInstances = 32
maxParticlesPerInstance = 256
maxGlobalParticles = 4096
```

Quando atingir o limite:

1. Remover instâncias já quase invisíveis.
2. Depois remover a instância mais antiga.
3. Nunca aumentar memória dinamicamente sem limite.

Cada instância mantém a revisão do programa usada quando nasceu. Aplicar uma nova revisão afeta somente cliques futuros.

## Multi-monitor

As posições devem continuar em coordenadas lógicas do KWin. Em renderização:

- Converter pela escala do `RenderViewport`.
- Calcular bounds de cada instância.
- Desenhar somente se intersectar o output atual.
- Solicitar repaint somente das regiões afetadas.
- Preparar variantes de textura por escala quando necessário.

# Limites de segurança recomendados

| Recurso | Limite inicial |
|---|---:|
| Tamanho do JSON | 256 KiB |
| Layers por efeito | 32 |
| Tracks por layer | 24 |
| Keyframes por track | 64 |
| Duração máxima | 10 segundos |
| Partículas por layer/clique | 256 |
| Partículas globais | 4096 |
| Instâncias simultâneas | 32 |
| Assets por efeito | 32 |
| Lados de polígono | 64 |
| Resolução padrão de textura | 2048×2048 |
| Resolução máxima absoluta | 4096×4096 |
| Memória GPU por efeito | 64 MiB |
| Tamanho extraído de `.radiantcursor` | 128 MiB |

Outras proteções:

- Recusar `..`, symlinks e caminhos absolutos.
- Validar MIME pelo conteúdo.
- Limitar dimensões antes de decodificar.
- Proteger contra ZIP bombs.
- Recusar `NaN`, infinito e números extremos.
- Limitar `easeOutElastic` e similares para não gerar bounds infinitos.
- Fuzzing do parser C++.
- Manter o último programa válido em qualquer falha.

# Formato `.radiantcursor`

Vale a pena criar.

```text
anime-click.radiantcursor
├── manifest.json
├── effect.json
├── metadata.json
└── assets/
    ├── abc123.png
    └── def456.webp
```

O processo principal do Electron deve importar o ZIP em uma pasta temporária, validar tudo e só então publicar na biblioteca. O plugin C++ nunca deve abrir ZIPs.

# Versionamento e migrações

Recomendo três versões independentes:

```json
{
  "schemaVersion": 1,
  "minimumEngineVersion": 1,
  "revision": "sha256:..."
}
```

- `schemaVersion`: formato do documento.
- `minimumEngineVersion`: recursos necessários no plugin.
- `revision`: conteúdo imutável.

Migração no TypeScript:

```text
v1 → v2 → v3
```

O editor preserva o original antes de migrar. O plugin deve aceitar somente schemas explicitamente suportados; não deve tentar “adivinhar” campos desconhecidos.

Também recomendo `requiredCapabilities`, permitindo mensagens melhores:

```json
{
  "requiredCapabilities": [
    "image.spriteSheet.v1",
    "blend.additive.v1"
  ]
}
```

# Estrutura de diretórios do código

```text
src/
├── shared/
│   ├── engine/
│   │   ├── schema.ts
│   │   ├── layers.ts
│   │   ├── animation.ts
│   │   ├── assets.ts
│   │   ├── limits.ts
│   │   └── migrations/
│   └── ipc/
├── main/
│   ├── engine/
│   │   ├── effect-repository.ts
│   │   ├── asset-store.ts
│   │   ├── bundle-importer.ts
│   │   ├── bundle-exporter.ts
│   │   ├── validator.ts
│   │   └── deployment.ts
│   └── ipc/
└── renderer/
    ├── editor/
    │   ├── EditorShell.tsx
    │   ├── layers/
    │   ├── properties/
    │   ├── timeline/
    │   ├── preview/
    │   ├── library/
    │   └── state/
    └── components/

native/kwin/
├── engine/
│   ├── schema/
│   ├── compiler/
│   ├── runtime/
│   ├── animation/
│   ├── assets/
│   ├── cache/
│   └── limits/
├── renderer/
│   ├── renderqueue.*
│   ├── shaperenderer.*
│   ├── imagerenderer.*
│   └── particlerenderer.*
├── input/
│   └── clickcapture.*
└── plugin/
    └── radiantcursoreffect.*
```

# Editor visual

A estrutura proposta no pedido faz sentido:

```text
Layers | Preview | Properties
          Timeline
```

Eu adicionaria:

- Barra superior com efeito/revisão atual.
- Indicador “draft” versus “aplicado no KWin”.
- Painel de erros do compilador.
- Budget meter para layers, partículas e memória.
- Undo/redo por comandos.
- Autosave do draft, separado do deploy.
- Preview com resolução e escala configuráveis.

Drag-and-drop deve alterar somente a ordem do array de layers. IDs precisam permanecer estáveis para timeline, undo e seleção.

# Fases de implementação

## Fase 0 — Contratos e testes

- Congelar limites iniciais.
- Criar schema TypeScript.
- Criar fixtures JSON válidas e inválidas.
- Definir resultados esperados para easings.
- Definir testes de bounds e partículas.
- Não mexer ainda nos efeitos atuais.

## Fase 1 — Repositório e deploy

- Effect repository.
- Asset store por hash.
- Escrita atômica.
- Revisões imutáveis.
- IPC para salvar, carregar e aplicar.
- `kwinrc` passa a guardar somente efeito/revisão ativa.

## Fase 2 — Parser e compiler C++

- Parser restrito.
- Validação duplicada.
- `CompiledEffect`.
- Tracks e easings.
- Last-known-good program.
- Diagnósticos estruturados.

## Fase 3 — Shape engine

- Shape layer.
- Mesh cache.
- Materiais sólidos.
- Contorno e preenchimento.
- Transformações.
- Normal/additive blend.
- Migrar Ondas, Pulso e Alvo como prova.

## Fase 4 — Particle engine

- Emissores.
- Distribuições.
- Seeds determinísticos.
- Quatro variações.
- Particle pool.
- Gravity/drag.
- Migrar Confete, Bolhas, Pixels e rastros.

## Fase 5 — Editor básico

- Layers.
- Properties.
- Duplicar, ocultar, remover e ordenar.
- Preview Canvas/WebGL.
- Play, pause e restart.
- Undo/redo.

## Fase 6 — Imagens e caches

- Importação segura.
- PNG/JPEG/WebP.
- Rasterização de SVG.
- Texture cache.
- Imagens por botão.
- Blend modes.
- Medidor de memória.

## Fase 7 — Timeline

- Keyframes.
- Scrub.
- Zoom.
- Alteração de duração.
- Easing editor.
- Seleção múltipla de keyframes.

## Fase 8 — Biblioteca e `.radiantcursor`

- My Effects.
- Installed.
- Favorites.
- Import/export.
- ZIP seguro.
- Thumbnails.
- Migrações de schema.

## Fase 9 — Migração e otimização

- Converter os efeitos prontos para JSON.
- Remover dispatch C++ legado.
- Batching.
- Benchmarks em 4K/high refresh.
- Testes de click storm.
- Testes multi-monitor.
- Fuzzing e recuperação de falhas.

# Recomendação final

Não comece pelo editor visual completo. O primeiro marco deve ser:

1. Schema v1.
2. Repositório de revisões.
3. Compiler C++.
4. Shape layer.
5. Três efeitos atuais convertidos para JSON.
6. Execução simultânea com fallback para o runtime legado.

Quando esses três efeitos tiverem paridade visual e estabilidade, avance para partículas, imagens e timeline.

# Estado da implementação — v4.0.0

## Concluído

- [x] Fase 0: schema v1, limites, easings, fixtures e testes automatizados.
- [x] Fase 1: drafts, repositório, revisões SHA-256, escrita atômica e IPC seguro.
- [x] Fase 2: parser/compiler C++ e last-known-good com fallback legado.
- [x] Fase 3: shape engine com contorno, preenchimento, transformações e blend modes.
- [x] Fase 4: particle engine com seed determinística, gravity, drag e quatro variações.
- [x] Fase 5: editor com Biblioteca, Layers, Canvas, Propriedades, Timeline e undo/redo.
- [x] Fase 6: PNG/JPEG/WebP, rasterização de SVG, store por hash e layer de imagem no KWin.
- [x] Fase 7: tracks numéricas, criação/remoção de keyframes, easings, scrub e marcadores.
- [x] Fase 8: biblioteca local e `.radiantcursor` com import/export ZIP seguro.
- [x] Migração dos 28 efeitos de clique para documentos declarativos iniciais.
- [x] Interface sem scrollbar global; painéis usam scroll interno e respeitam a janela mínima.
- [x] Abas legadas de clique e dos 20 rastros preservadas durante a estabilização.

## Validação já executada

- [x] `npm run typecheck`.
- [x] `npm test`, incluindo round-trip `.radiantcursor` e bloqueio de path traversal.
- [x] `npm run build`.
- [x] Build C++ do plugin contra os headers locais do KWin/Plasma 6.
- [x] Verificação explícita de Plasma 6 no instalador do plugin.

## Portão de estabilização no computador real

- [ ] Instalar/recarregar o novo `radiantcursor.so` e validar os 28 efeitos em uma sessão KWin real.
- [ ] Medir click storm em 4K/high refresh e múltiplos monitores.
- [ ] Comparar visualmente o Canvas e o renderer nativo para ajustar fixtures de conformidade.
- [ ] Depois desses testes, remover o dispatch legado e migrar os 20 rastros para documentos próprios.
- [ ] Adicionar batching/VBO persistente e fuzzing prolongado antes de chamar o runtime de estável.

O dispatch legado continua propositalmente como fallback. Removê-lo antes do teste ao vivo contrariaria o requisito de não colocar o compositor em risco.

Essa ordem valida o ponto mais arriscado — compiler/runtime dentro do KWin — antes de investir no editor inteiro.
