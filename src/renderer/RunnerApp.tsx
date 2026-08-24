import { useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import {
  DEFAULT_RADIANT_CURSOR_SETTINGS,
  type ClickEffectStyle,
  type RadiantCursorSettings,
  type RadiantCursorState,
  type TrailStyle,
} from "../shared/types";
import { Icon } from "./components/Icon";
import radiantCursorIcon from "../../rc-icon.png";
import radiantCursorBanner from "../../rc-banner.png";

const CLICK_EFFECTS: Array<[ClickEffectStyle, string, string, string]> = [
  ["ripple", "Ondas", "Anéis em expansão", "◎"], ["pulse", "Pulso", "Disco energético", "◉"],
  ["target", "Alvo", "Mira de precisão", "⌾"], ["burst", "Explosão", "Raios radiais", "✺"],
  ["spark", "Faísca", "Estrela dinâmica", "✦"], ["focus", "Foco", "Cantos de enquadre", "⌗"],
  ["halo", "Halo", "Brilho atmosférico", "◌"], ["shockwave", "Impacto", "Onda de choque", "⊙"],
  ["orbit", "Órbita", "Satélites giratórios", "◍"], ["petals", "Pétalas", "Flor em expansão", "❉"],
  ["diamond", "Diamante", "Losangos concêntricos", "◇"], ["sonar", "Sonar", "Varredura direcional", "◔"],
  ["vortex", "Vórtice", "Espiral rotativa", "◴"], ["cross", "Cruz", "Impacto ortogonal", "✛"],
  ["confetti", "Confete", "Fragmentos coloridos", "✣"], ["lightning", "Relâmpago", "Descarga elétrica", "ϟ"],
  ["bubbles", "Bolhas", "Orbes flutuantes", "⚬"], ["heart", "Coração", "Pulso afetivo", "♡"],
  ["ink", "Tinta", "Mancha preenchida", "●"], ["splash", "Splash", "Gotas orgânicas", "✽"],
  ["nova", "Supernova", "Explosão sólida", "✹"], ["comet", "Cometa", "Núcleo e cauda", "☄"],
  ["eclipse", "Eclipse", "Discos sobrepostos", "◒"], ["plasma", "Plasma", "Forma fluida", "≈"],
  ["pixelburst", "Pixels", "Blocos digitais", "▦"], ["prism", "Prisma", "Triângulos cromáticos", "△"],
  ["flower", "Flor cheia", "Pétalas preenchidas", "✿"], ["meteor", "Meteoro", "Impacto veloz", "☄"],
];

const TRAILS: Array<[TrailStyle, string, string, string]> = [
  ["dots", "Pontos", "Partículas limpas", "••"], ["soft", "Nuvem suave", "Rastro difuso", "☁"],
  ["neon", "Neon", "Brilho luminoso", "⌁"], ["cometTrail", "Cometa", "Cauda contínua", "☄"],
  ["smoke", "Fumaça", "Volume disperso", "≋"], ["sparks", "Faíscas", "Partículas elétricas", "✦"],
  ["bubbleTrail", "Bolhas", "Orbes leves", "⚬"], ["stars", "Estrelas", "Cintilante", "★"],
  ["hearts", "Corações", "Flutuante", "♥"], ["squares", "Quadrados", "Geométrico", "▪"],
  ["diamonds", "Diamantes", "Geométrico", "◆"], ["triangles", "Triângulos", "Direcional", "▲"],
  ["ribbon", "Fita", "Faixa contínua", "〰"], ["laser", "Laser", "Feixe preciso", "━"],
  ["fire", "Fogo", "Partículas quentes", "♨"], ["ice", "Gelo", "Cristais frios", "❄"],
  ["petalTrail", "Pétalas", "Orgânico", "❉"], ["pixels", "Pixels", "Digital", "▦"],
  ["orbitTrail", "Órbita", "Satélites", "◉"], ["rainbow", "Arco-íris", "Cromático", "⌒"],
];

const FILLED_EFFECTS = new Set<ClickEffectStyle>([
  "ink", "splash", "nova", "comet", "eclipse", "plasma", "pixelburst", "prism", "flower", "meteor",
]);

type Tab = "click" | "trail";
type ClickFilter = "all" | "outline" | "filled";
type Update = <K extends keyof RadiantCursorSettings>(key: K, value: RadiantCursorSettings[K]) => void;

export function RunnerApp() {
  const [tab, setTab] = useState<Tab>("click");
  const [filter, setFilter] = useState<ClickFilter>("all");
  const [query, setQuery] = useState("");
  const [pickerOpen, setPickerOpen] = useState(false);
  const [settings, setSettings] = useState<RadiantCursorSettings>({ ...DEFAULT_RADIANT_CURSOR_SETTINGS });
  const [saved, setSaved] = useState<RadiantCursorSettings>({ ...DEFAULT_RADIANT_CURSOR_SETTINGS });
  const [state, setState] = useState<RadiantCursorState | null>(null);
  const [busy, setBusy] = useState(true);
  const [message, setMessage] = useState("");
  const pickerRef = useRef<HTMLDivElement>(null);

  const dirty = useMemo(() => JSON.stringify(settings) !== JSON.stringify(saved), [settings, saved]);
  const windowsRuntime = state?.compatibility.platform === "windows" || (!state && navigator.userAgent.includes("Windows"));
  const runtimeName = windowsRuntime ? "Windows" : "KWin";
  const update: Update = (key, value) => setSettings((current) => ({ ...current, [key]: value }));
  const notify = (value: string) => {
    setMessage(value);
    window.setTimeout(() => setMessage(""), 3500);
  };

  useEffect(() => {
    let dead = false;
    void (async () => {
      if (!window.radiantcursor) {
        setBusy(false);
        return;
      }
      try {
        const next = await window.radiantcursor.getState();
        if (!dead) {
          setState(next);
          setSettings({ ...next.settings });
          setSaved({ ...next.settings });
        }
      } catch (error) {
        notify(error instanceof Error ? error.message : String(error));
      } finally {
        if (!dead) setBusy(false);
      }
    })();
    return () => { dead = true; };
  }, []);

  useEffect(() => {
    const closePicker = (event: PointerEvent) => {
      if (!pickerRef.current?.contains(event.target as Node)) setPickerOpen(false);
    };
    const closeWithEscape = (event: KeyboardEvent) => {
      if (event.key === "Escape") setPickerOpen(false);
    };
    window.addEventListener("pointerdown", closePicker);
    window.addEventListener("keydown", closeWithEscape);
    return () => {
      window.removeEventListener("pointerdown", closePicker);
      window.removeEventListener("keydown", closeWithEscape);
    };
  }, []);

  const apply = async (activate: boolean) => {
    if (!window.radiantcursor) return notify("Abra pelo Electron para aplicar o efeito no sistema");
    setBusy(true);
    try {
      const next = activate
        ? await window.radiantcursor.activateEffect(settings)
        : await window.radiantcursor.applySettings(settings);
      setState(next);
      setSaved({ ...settings });
      notify(activate ? "RadiantCursor aplicado e ativo" : "Configuração aplicada");
    } catch (error) {
      notify(error instanceof Error ? error.message : String(error));
    } finally {
      setBusy(false);
    }
  };

  const disable = async () => {
    if (!window.radiantcursor) return;
    setBusy(true);
    try {
      setState(await window.radiantcursor.disableEffect());
      notify("RadiantCursor desativado");
    } catch (error) {
      notify(error instanceof Error ? error.message : String(error));
    } finally {
      setBusy(false);
    }
  };

  const rawList = tab === "click" ? CLICK_EFFECTS : TRAILS;
  const selectedId = tab === "click" ? settings.Style : settings.TrailStyle;
  const selected = rawList.find(([id]) => id === selectedId) ?? rawList[0]!;
  const featureEnabled = tab === "click" ? settings.ClickEnabled : settings.TrailEnabled;
  const normalizedQuery = query.trim().toLocaleLowerCase("pt-BR");
  const pickerItems = rawList.filter(([id, name, description]) => {
    const matchesQuery = !normalizedQuery || `${name} ${description}`.toLocaleLowerCase("pt-BR").includes(normalizedQuery);
    const matchesFilter = tab === "trail" || filter === "all"
      || (filter === "filled" ? FILLED_EFFECTS.has(id as ClickEffectStyle) : !FILLED_EFFECTS.has(id as ClickEffectStyle));
    return matchesQuery && matchesFilter;
  });

  const switchTab = (next: Tab) => {
    setTab(next);
    setQuery("");
    setFilter("all");
    setPickerOpen(false);
  };

  const selectEffect = (id: ClickEffectStyle | TrailStyle) => {
    if (tab === "click") update("Style", id as ClickEffectStyle);
    else update("TrailStyle", id as TrailStyle);
    setPickerOpen(false);
  };

  const toggleFeature = () => {
    if (tab === "click") update("ClickEnabled", !settings.ClickEnabled);
    else update("TrailEnabled", !settings.TrailEnabled);
  };

  return (
    <div className="runner-shell runner-compact">
      <header className="compact-header">
        <div className="compact-brand"><span><img src={radiantCursorIcon} alt="" /></span><div><strong>RadiantCursor</strong><small>Cliques e rastros para o sistema</small></div></div>
        <div className="window-actions"><button className={`kwin-switch ${state?.isLoaded ? "active" : ""}`} disabled={busy} onClick={() => state?.isLoaded ? void disable() : void apply(true)} title={state?.isLoaded ? `Desativar no ${runtimeName}` : `Ativar no ${runtimeName}`}><i />{busy ? "Aguarde…" : state?.isLoaded ? `Ativo no ${runtimeName}` : `Ativar no ${runtimeName}`}</button><i className="window-divider" /><button className="window-button" title="Minimizar" aria-label="Minimizar janela" onClick={() => void window.radiantcursor?.minimizeWindow()}><Icon name="minus" size={14} /></button><button className="window-button close" title="Fechar" aria-label="Fechar janela" onClick={() => void window.radiantcursor?.closeWindow()}><Icon name="close" size={14} /></button></div>
      </header>

      <main className="compact-main">
        <div className="compact-banner" aria-hidden="true">
          <img src={radiantCursorBanner} alt="" />
        </div>

        <nav className="compact-tabs" aria-label="Área de personalização">
          <button className={tab === "click" ? "active" : ""} onClick={() => switchTab("click")}><Icon name="pointer" size={13} /> Cliques</button>
          <button className={tab === "trail" ? "active" : ""} onClick={() => switchTab("trail")}><Icon name="sparkles" size={13} /> Rastro</button>
        </nav>

        <section className="quick-config">
          <div className="effect-control-row">
            <div className="effect-picker" ref={pickerRef}>
              <label>{tab === "click" ? "Efeito" : "Estilo do rastro"}</label>
              <button className="effect-select" onClick={() => setPickerOpen((value) => !value)} aria-expanded={pickerOpen}><i>{selected[3]}</i><span><strong>{selected[1]}</strong><small>{selected[2]}</small></span><Icon name="chevron" size={13} /></button>
              {pickerOpen && (
                <div className="effect-popover">
                  <label className="effect-search"><Icon name="search" size={13} /><input autoFocus value={query} onChange={(event) => setQuery(event.target.value)} placeholder={tab === "click" ? "Buscar efeito…" : "Buscar rastro…"} /></label>
                  {tab === "click" && <div className="effect-filters"><button className={filter === "all" ? "active" : ""} onClick={() => setFilter("all")}>Todos</button><button className={filter === "outline" ? "active" : ""} onClick={() => setFilter("outline")}>Contorno</button><button className={filter === "filled" ? "active" : ""} onClick={() => setFilter("filled")}>Preenchidos</button></div>}
                  <div className="effect-options">
                    {pickerItems.map(([id, name, description, mark]) => <button key={id} className={id === selectedId ? "selected" : ""} onClick={() => selectEffect(id)} title={description}><i>{mark}</i><span>{name}</span>{id === selectedId && <Icon name="check" size={12} />}</button>)}
                    {!pickerItems.length && <div className="effect-empty">Nenhum resultado</div>}
                  </div>
                </div>
              )}
            </div>
            <button className={`feature-switch ${featureEnabled ? "enabled" : ""}`} onClick={toggleFeature}><span><strong>{tab === "click" ? "Cliques" : "Rastro"}</strong><small>{featureEnabled ? "Ligado" : "Desligado"}</small></span><i><b /></i></button>
          </div>

          <div className="quick-settings">
            {tab === "click" ? <ClickSettings settings={settings} update={update} /> : <TrailSettings settings={settings} update={update} />}
          </div>
        </section>
      </main>

      <footer className="compact-footer">
        <span className={dirty ? "changed" : ""}><i />{dirty ? "Alterações não aplicadas" : state?.isLoaded ? "Configuração aplicada" : "Pronto para ativar"}</span>
        <button className="compact-reset" onClick={() => setSettings({ ...DEFAULT_RADIANT_CURSOR_SETTINGS })} title="Restaurar padrões"><Icon name="reset" size={13} /></button>
        <button className="compact-apply" disabled={busy || (Boolean(state?.isLoaded) && !dirty)} onClick={() => void apply(!state?.isLoaded)}>{busy ? "Aplicando…" : state?.isLoaded ? "Aplicar" : "Aplicar e ativar"}</button>
      </footer>

      {message && <div className="runner-toast"><Icon name="info" size={13} />{message}</div>}
    </div>
  );
}

function SettingsSection({ title, children }: { title: string; children: ReactNode }) {
  return <section className="compact-more"><div className="compact-more-title">{title}</div><div>{children}</div></section>;
}

function Toggle({ label, value, onChange }: { label: string; value: boolean; onChange: (value: boolean) => void }) {
  return <button type="button" className="compact-toggle" aria-pressed={value} onClick={() => onChange(!value)}><span>{label}</span><i className={value ? "on" : ""}><b /></i></button>;
}

function Range({ label, value, min, max, step = 1, suffix = "", display, onChange }: { label: string; value: number; min: number; max: number; step?: number; suffix?: string; display?: (value: number) => string; onChange: (value: number) => void }) {
  const output = display ? display(value) : `${Math.round(value * 100) / 100}${suffix}`;
  return <label className="compact-range"><span>{label}</span><input type="range" min={min} max={max} step={step} value={value} onChange={(event) => onChange(Number(event.target.value))} /><output>{output}</output></label>;
}

function Color({ label, shortLabel, value, onChange }: { label: string; shortLabel: string; value: string; onChange: (value: string) => void }) {
  return <label className="compact-color" title={`${label}: ${value}`}><input type="color" value={value} onChange={(event) => onChange(event.target.value)} /><span>{shortLabel}</span></label>;
}

function ClickSettings({ settings, update }: { settings: RadiantCursorSettings; update: Update }) {
  return <>
    <div className="essential-settings">
      <div className="compact-colors"><strong>Cores</strong><div><Color label="Botão esquerdo" shortLabel="E" value={settings.Color1} onChange={(value) => update("Color1", value)} /><Color label="Botão do meio" shortLabel="M" value={settings.Color2} onChange={(value) => update("Color2", value)} /><Color label="Botão direito" shortLabel="D" value={settings.Color3} onChange={(value) => update("Color3", value)} /></div></div>
      <CompactRanges><Range label="Tamanho" value={settings.RingSize} min={5} max={220} suffix=" px" onChange={(value) => update("RingSize", value)} /><Range label="Duração" value={settings.RingLife} min={80} max={1800} step={10} suffix=" ms" onChange={(value) => update("RingLife", value)} /></CompactRanges>
    </div>
    <SettingsSection title="Mais opções">
      <div className="more-grid">
        <TriggerSelect value={settings.Trigger} onChange={(value) => update("Trigger", value)} />
        <CompactRanges><Range label="Quantidade" value={settings.RingCount} min={1} max={12} onChange={(value) => update("RingCount", value)} /><Range label="Espessura" value={settings.LineWidth} min={0.5} max={12} step={0.5} suffix=" px" onChange={(value) => update("LineWidth", value)} /></CompactRanges>
        <div className="toggle-pair"><Toggle label="Brilho" value={settings.Glow} onChange={(value) => update("Glow", value)} /><Toggle label="Mostrar botão" value={settings.ShowText} onChange={(value) => update("ShowText", value)} /></div>
      </div>
    </SettingsSection>
  </>;
}

function TrailSettings({ settings, update }: { settings: RadiantCursorSettings; update: Update }) {
  return <>
    <div className="essential-settings trail-settings">
      <div className="compact-colors"><strong>Cor</strong><div><Color label="Cor das partículas" shortLabel="Rastro" value={settings.TrailColor} onChange={(value) => update("TrailColor", value)} /></div></div>
      <CompactRanges><Range label="Tamanho" value={settings.TrailSize} min={2} max={80} suffix=" px" onChange={(value) => update("TrailSize", value)} /><Range label="Duração" value={settings.TrailLife} min={80} max={2500} step={10} suffix=" ms" onChange={(value) => update("TrailLife", value)} /><Range label="Frequência" value={settings.TrailFrequency} min={1} max={120} suffix=" Hz" onChange={(value) => update("TrailFrequency", value)} /></CompactRanges>
    </div>
    <SettingsSection title="Mais opções">
      <div className="more-grid trail-more">
        <CompactRanges><Range label="Densidade" value={settings.TrailDensity} min={1} max={100} suffix="%" onChange={(value) => update("TrailDensity", value)} /><Range label="Opacidade" value={settings.TrailOpacity} min={0.05} max={1} step={0.05} display={(value) => `${Math.round(value * 100)}%`} onChange={(value) => update("TrailOpacity", value)} /></CompactRanges>
        <div className="toggle-pair"><Toggle label="Brilho" value={settings.TrailGlow} onChange={(value) => update("TrailGlow", value)} /><Toggle label="Só durante clique" value={settings.TrailOnlyPressed} onChange={(value) => update("TrailOnlyPressed", value)} /></div>
      </div>
    </SettingsSection>
  </>;
}

function CompactRanges({ children }: { children: ReactNode }) {
  return <div className="compact-ranges">{children}</div>;
}

function TriggerSelect({ value, onChange }: { value: RadiantCursorSettings["Trigger"]; onChange: (value: RadiantCursorSettings["Trigger"]) => void }) {
  const [open, setOpen] = useState(false);
  const root = useRef<HTMLDivElement>(null);
  const options: Array<[RadiantCursorSettings["Trigger"], string]> = [["press", "Ao pressionar"], ["release", "Ao soltar"], ["both", "Nos dois"]];
  const label = options.find(([id]) => id === value)?.[1] ?? options[0]![1];

  useEffect(() => {
    const close = (event: PointerEvent) => {
      if (!root.current?.contains(event.target as Node)) setOpen(false);
    };
    const escape = (event: KeyboardEvent) => {
      if (event.key === "Escape") setOpen(false);
    };
    window.addEventListener("pointerdown", close);
    window.addEventListener("keydown", escape);
    return () => {
      window.removeEventListener("pointerdown", close);
      window.removeEventListener("keydown", escape);
    };
  }, []);

  return <div className="compact-select" ref={root}><span>Disparo</span><button type="button" className="compact-select-button" aria-haspopup="listbox" aria-expanded={open} onClick={() => setOpen((current) => !current)}><span>{label}</span><Icon name="chevron" size={12} /></button>{open && <div className="compact-listbox" role="listbox" aria-label="Disparo do efeito">{options.map(([id, optionLabel]) => <button type="button" role="option" aria-selected={id === value} className={id === value ? "selected" : ""} key={id} onClick={() => { onChange(id); setOpen(false); }}><span>{optionLabel}</span>{id === value && <Icon name="check" size={12} />}</button>)}</div>}</div>;
}
