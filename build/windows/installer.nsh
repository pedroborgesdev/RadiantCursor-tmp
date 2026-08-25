!include "LogicLib.nsh"

!macro customUnInstall
  ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "RadiantCursor Runtime"
  StrLen $2 "$INSTDIR"
  StrCpy $1 $0 $2
  ${If} $1 == "$INSTDIR"
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "RadiantCursor Runtime"
    nsExec::ExecToLog '"$INSTDIR\resources\runtime\windows\RadiantCursor.Runtime.exe" --stop'
  ${Else}
    ; Electron envolve caminhos com espaços em aspas. Compare também após a
    ; primeira aspa para não remover o autorun de outra instalação.
    StrCpy $1 $0 $2 1
    ${If} $1 == "$INSTDIR"
      DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "RadiantCursor Runtime"
      nsExec::ExecToLog '"$INSTDIR\resources\runtime\windows\RadiantCursor.Runtime.exe" --stop'
    ${EndIf}
  ${EndIf}
!macroend
