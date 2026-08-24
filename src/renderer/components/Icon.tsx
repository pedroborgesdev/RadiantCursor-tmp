import type { SVGProps } from 'react';

export type IconName =
  | 'alert'
  | 'check'
  | 'chevron'
  | 'close'
  | 'download'
  | 'info'
  | 'monitor'
  | 'palette'
  | 'play'
  | 'pointer'
  | 'power'
  | 'refresh'
  | 'reset'
  | 'save'
  | 'sparkles'
  | 'trash'
  | 'type'
  | 'layers'
  | 'plus'
  | 'copy'
  | 'eye'
  | 'eyeOff'
  | 'undo'
  | 'redo'
  | 'circle'
  | 'particles'
  | 'pause'
  | 'search'
  | 'clock'
  | 'sliders'
  | 'library'
  | 'minus';

interface IconProps extends SVGProps<SVGSVGElement> {
  name: IconName;
  size?: number;
}

export function Icon({ name, size = 18, ...props }: IconProps) {
  const common = {
    width: size,
    height: size,
    viewBox: '0 0 24 24',
    fill: 'none',
    stroke: 'currentColor',
    strokeWidth: 1.8,
    strokeLinecap: 'round' as const,
    strokeLinejoin: 'round' as const,
    'aria-hidden': true,
  };

  return (
    <svg {...common} {...props}>
      {name === 'alert' && (
        <>
          <path d="M10.3 3.7 2.6 17a2 2 0 0 0 1.7 3h15.4a2 2 0 0 0 1.7-3L13.7 3.7a2 2 0 0 0-3.4 0Z" />
          <path d="M12 9v4" />
          <path d="M12 17h.01" />
        </>
      )}
      {name === 'check' && <path d="m5 12 4 4L19 6" />}
      {name === 'chevron' && <path d="m7 10 5 5 5-5" />}
      {name === 'close' && (
        <>
          <path d="m6 6 12 12" />
          <path d="m18 6-12 12" />
        </>
      )}
      {name === 'download' && (
        <>
          <path d="M12 3v12" />
          <path d="m7 10 5 5 5-5" />
          <path d="M5 21h14" />
        </>
      )}
      {name === 'info' && (
        <>
          <circle cx="12" cy="12" r="9" />
          <path d="M12 11v5" />
          <path d="M12 8h.01" />
        </>
      )}
      {name === 'monitor' && (
        <>
          <rect x="3" y="4" width="18" height="13" rx="2" />
          <path d="M8 21h8" />
          <path d="M12 17v4" />
        </>
      )}
      {name === 'palette' && (
        <>
          <path d="M12 3a9 9 0 0 0 0 18h1.3a1.7 1.7 0 0 0 1.2-2.9 1.7 1.7 0 0 1 1.2-2.9H18A3 3 0 0 0 21 12a9 9 0 0 0-9-9Z" />
          <circle cx="7.5" cy="10.5" r=".8" fill="currentColor" stroke="none" />
          <circle cx="10" cy="7" r=".8" fill="currentColor" stroke="none" />
          <circle cx="14.5" cy="7.5" r=".8" fill="currentColor" stroke="none" />
        </>
      )}
      {name === 'play' && <path d="m8 5 11 7-11 7Z" />}
      {name === 'pointer' && (
        <>
          <path d="m5 3 12 9-5.6 1.2L9 19Z" />
          <path d="m13 14 4 6" />
        </>
      )}
      {name === 'power' && (
        <>
          <path d="M12 2v10" />
          <path d="M18.4 6.6a9 9 0 1 1-12.8 0" />
        </>
      )}
      {name === 'refresh' && (
        <>
          <path d="M20 6v5h-5" />
          <path d="M4 18v-5h5" />
          <path d="M6.1 8a7 7 0 0 1 11.5-2L20 8" />
          <path d="m4 16 2.4 2A7 7 0 0 0 18 16" />
        </>
      )}
      {name === 'reset' && (
        <>
          <path d="M3 12a9 9 0 1 0 3-6.7L3 8" />
          <path d="M3 3v5h5" />
        </>
      )}
      {name === 'save' && (
        <>
          <path d="M5 3h12l2 2v16H5Z" />
          <path d="M8 3v6h8V3" />
          <path d="M8 21v-7h8v7" />
        </>
      )}
      {name === 'sparkles' && (
        <>
          <path d="m12 3 1.2 3.1L16 7.4l-2.8 1.2L12 12l-1.2-3.4L8 7.4l2.8-1.3Z" />
          <path d="m18.5 13 .7 1.8 1.8.7-1.8.8-.7 1.7-.7-1.7-1.8-.8 1.8-.7Z" />
          <path d="m5.5 14 .9 2.1 2.1.9-2.1.9-.9 2.1-.9-2.1-2.1-.9 2.1-.9Z" />
        </>
      )}
      {name === 'trash' && (
        <>
          <path d="M4 7h16" />
          <path d="M9 3h6l1 4H8Z" />
          <path d="m6 7 1 14h10l1-14" />
          <path d="M10 11v6M14 11v6" />
        </>
      )}
      {name === 'type' && (
        <>
          <path d="M4 5V3h16v2" />
          <path d="M9 21h6" />
          <path d="M12 3v18" />
        </>
      )}
      {name === 'layers' && (<><path d="m12 2 9 5-9 5-9-5Z" /><path d="m3 12 9 5 9-5" /><path d="m3 17 9 5 9-5" /></>)}
      {name === 'plus' && (<><path d="M12 5v14" /><path d="M5 12h14" /></>)}
      {name === 'copy' && (<><rect x="8" y="8" width="11" height="11" rx="2" /><path d="M16 8V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h3" /></>)}
      {name === 'eye' && (<><path d="M2 12s3.5-6 10-6 10 6 10 6-3.5 6-10 6S2 12 2 12Z" /><circle cx="12" cy="12" r="2.5" /></>)}
      {name === 'eyeOff' && (<><path d="m3 3 18 18" /><path d="M10.6 6.2A11 11 0 0 1 12 6c6.5 0 10 6 10 6a17 17 0 0 1-2.1 2.8M6.2 6.2C3.5 8 2 12 2 12s3.5 6 10 6a10 10 0 0 0 4-.8" /><path d="M9.8 9.8a3 3 0 0 0 4.4 4.4" /></>)}
      {name === 'undo' && (<><path d="m9 7-5 5 5 5" /><path d="M20 17a7 7 0 0 0-7-7H4" /></>)}
      {name === 'redo' && (<><path d="m15 7 5 5-5 5" /><path d="M4 17a7 7 0 0 1 7-7h9" /></>)}
      {name === 'circle' && <circle cx="12" cy="12" r="8" />}
      {name === 'particles' && (<><circle cx="8" cy="8" r="2" /><circle cx="17" cy="6" r="1.5" /><circle cx="15" cy="15" r="3" /><circle cx="6" cy="17" r="1" /></>)}
      {name === 'pause' && (<><path d="M9 5v14" /><path d="M15 5v14" /></>)}
      {name === 'search' && (<><circle cx="11" cy="11" r="7" /><path d="m20 20-4-4" /></>)}
      {name === 'clock' && (<><circle cx="12" cy="12" r="9" /><path d="M12 7v5l3 2" /></>)}
      {name === 'sliders' && (<><path d="M4 6h16M4 12h16M4 18h16" /><circle cx="8" cy="6" r="2" fill="currentColor" stroke="none" /><circle cx="16" cy="12" r="2" fill="currentColor" stroke="none" /><circle cx="10" cy="18" r="2" fill="currentColor" stroke="none" /></>)}
      {name === 'library' && (<><path d="M4 4h4v16H4zM10 4h4v16h-4zM16 5l4-1 2 15-4 1z" /></>)}
      {name === 'minus' && <path d="M5 12h14" />}
    </svg>
  );
}
