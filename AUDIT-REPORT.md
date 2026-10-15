# AUDIT-REPORT — 125A Plugin Tester v0.2.9

## 1. Ergebnis, Geltungsbereich und Freigabegrenzen

**Stand: 15. Oktober 2026 (Europe/Berlin), fortgesetzte Prüfung. Dokumentationsaudit; keine Reparaturfreigabe.**

**Aktueller Nachweisstand:** Die Erstprüfung bleibt als Verlauf erhalten. Die Fortsetzung in Abschnitt 12 ergänzt vier vorhandene erfolgreiche Windows-CI-Läufe samt Einzel-Schrittstatus und Artefaktmetadaten sowie 24 lokal ausgeführte Beobachtungen an unverändert extrahierter Logik. Vollständige CI-Logs, Binärdownloads, konkrete Nutzerpaketzuordnung, Windows-E2E-Reproduktionen der Befunde und Studio One bleiben offen. Abschnitt 13 enthält das reproduzierbare lokale Prüfverfahren.

Alle im folgenden Komponentenverzeichnis genannten produktiven Projektdateien und ihre Schnittstellen wurden statisch untersucht. Dieser Bericht ist ohne die vorausgehende Unterhaltung verständlich. Er dokumentiert **9 durch Codeanalyse bestätigte Fehler, 4 Verdachtsbefunde und 8 abgegrenzte Verbesserungsvorschläge**. Kein Befund wurde in dieser Untersuchung unter Windows reproduziert. Es gibt keine CRITICAL-Einstufung und keine pauschale Fehlerfreiheits-, QA- oder FINAL-Freigabe.

**Das vollständige technische Audit ist NICHT abgeschlossen:** CI-Lauf-, Schritt- und Artefaktmetadaten wurden inzwischen über reguläre GitHub-Webseiten geprüft; vollständige Logs, heruntergeladene Binärartefakte, Windows-E2E-Reproduktionen sowie weitergehende Audio-/MIDI- und Studio-One-Nachweise fehlen. Die statische Bestandsaufnahme ist durchgeführt. Eine vollständige Dokumentation des untersuchten Umfangs ist kein vollständiger Laufzeitnachweis.

Der Tester besitzt eine sinnvolle Grundlage aus Prozessisolation, offiziellem Validator und gezielten Fehler-Fixtures. Besonders relevant sind jedoch eigene Host-Vertragsverletzungen und widersprüchliche Ergebniswege: Ein FAIL kann vom Tester selbst verursacht werden; ein PASS kann unvollständig oder widersprüchlich sein. Deshalb dürfen Ergebnisse derzeit nur zusammen mit Einzelberichten, Exitcodes und tatsächlichem Prüfumfang beurteilt werden.

**Autorisierter Änderungsumfang:** ausschließlich diese neue Audit-Dokumentation auf einem separaten Dokumentationsbranch. Keine Änderungen an Programmcode, Tests, CMake, Workflow, Ressourcen, bestehenden Dokumenten, Versionen, Tags oder Releases. Der Erstauftrag erlaubte keinen automatischen Merge. Für die Fortsetzung ist ausschließlich das Zusammenführen eines geprüften reinen Dokumentations-PRs ausdrücklich genehmigt; kein neuer CI-Lauf. Vorschläge sind keine genehmigten Reparaturen.

## 2. Audit-Metadaten und Herkunft

| Merkmal | Nachweis / Status |
| --- | --- |
| Repository | https://github.com/challanger2000/125A-Plugin-Tester-Final |
| Untersuchte Referenz | main, per read-only git ls-remote bestätigt |
| Vollständiger Quellcommit | 513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321 |
| Ausgangsbranch der Cloud-Kopie | work, identischer HEAD; kein abweichender Entwicklungsstand angenommen |
| Dokumentationsbranch | docs/audit-plugin-tester-v0.2.9-2026-10-15 |
| Projektversion | CMake und Worker: 0.2.9; widersprechende Ressourcen siehe TESTER-004 |
| Letzter Commit mit technischen Änderungen | fd246146994384e301c735e7d860b53aa06b18c5, Committext „Promote Plugin Tester v0.2.9 to Final“, 2026-10-06 |
| Vergleich dieses Commits mit Prüfbasis | Alle 26 Dateien unter src/, tests/, CMakeLists.txt und aktuellem Workflow bytegleich. Dazwischen nur AGENTS.md/CURRENT.md ergänzt. Das beweist Quellgleichheit, keinen funktionierenden Windows-Build. |
| Letzte nachweislich funktionierende Windows-Version | **NOT VERIFIED** — weder Committext noch README belegen einen erfolgreichen Lauf |
| Nutzerpaket | 125A-Plugin-Tester-v0.2.9-Windows-x64; tatsächliche ZIP/EXE, SHA-256 und Build-Zuordnung **NOT VERIFIED** |
| Build-Plattform laut Workflow | windows-2022, Visual Studio 17 2022, x64, Release |
| SDK-Vorgabe | v3.8.1_build_84 aus CMake FetchContent |
| Lokal gelesener SDK-Root | 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96, Tag v3.8.1_build_84 |
| pluginterfaces-Submodul | 4f547e8e102b47de4a8b8aaf343c73b700786372 |
| public.sdk-Submodul | 586dc5e6c8012c3e4b01c79389375cbe96bdb1da |
| Engineering-Referenz | challanger2000/125A-Engineering, main 304ebeb6c965949b3e389adf8109f9bcb333e8f8 |
| Prüfmaschine | Linux; kein Visual Studio, Windows-Runner oder Studio One für dieses Audit verfügbar |
| Releaseklasse | Interne Windows-QA-Anwendung; kein kommerzielles Gumroad-FULL-Plugin |

### 2.1 Berücksichtigte Engineering-Vorgaben

Einstieg: [START-HERE.md](https://github.com/challanger2000/125A-Engineering/blob/304ebeb6c965949b3e389adf8109f9bcb333e8f8/START-HERE.md). Berücksichtigt wurden README, relevante Engineering Guardrails (Beweisgrenzen, Testzustandsverschmutzung, gültige Stimuli, GUI-/Audiothreads, Timing, Diagnose, State/SDK-Helfer, deaktivierte Release-Assertions, Artefaktzuordnung), STANDARDS/REPOSITORY-WORKFLOW.md, AI-ASSISTED-ENGINEERING.md, PROFESSIONAL-PLUGIN-BEHAVIOR.md, DSP-GUIDELINES.md, GUI-GUIDELINES.md, QA/RELEASE-QA.md, REALTIME-QA.md, AUDIO-FIXTURES.md, RESEARCH/OPEN-QUESTIONS.md, REJECTED-APPROACHES.md und ROADMAP/V3-CANDIDATES.md. RELEASE-REGISTER.md und lokales CURRENT.md liefern keinen belastbaren Paketnachweis.

Keine Branding-Neugestaltung, DSP-Implementierung oder kommerzielle FULL-/Demo-Auswahl war Gegenstand des Auftrags. Daher wurden dafür keine neuen Baselines ausgewählt. Die aktuelle Benutzeranweisung untersagt ausdrücklich automatische Korrekturen; auch Engineering-Guardrails wurden nicht verändert.

### 2.2 SDK-Vertragsquellen

- **SDK-A:** [AudioBusBuffers-Vertrag, ivstaudioprocessor.h:185–198](https://github.com/steinbergmedia/vst3_pluginterfaces/blob/4f547e8e102b47de4a8b8aaf343c73b700786372/vst/ivstaudioprocessor.h#L185-L198): Kanalzeiger-Array auch bei inaktiven Bussen erforderlich; einzelne Samplepuffer dürfen dort null sein.
- **SDK-B:** [setComponentHandler, ivsteditcontroller.h:528–531](https://github.com/steinbergmedia/vst3_pluginterfaces/blob/4f547e8e102b47de4a8b8aaf343c73b700786372/vst/ivsteditcontroller.h#L528-L531): verpflichtend bei Verwendung des Controllers.
- **SDK-C:** [Latenzabfrage:317–326](https://github.com/steinbergmedia/vst3_pluginterfaces/blob/4f547e8e102b47de4a8b8aaf343c73b700786372/vst/ivstaudioprocessor.h#L317-L326) und [Tail:348–360](https://github.com/steinbergmedia/vst3_pluginterfaces/blob/4f547e8e102b47de4a8b8aaf343c73b700786372/vst/ivstaudioprocessor.h#L348-L360): Zustand Setup Done.
- **SDK-D:** [EventList::addEvent, eventlist.cpp:65–73](https://github.com/steinbergmedia/vst3_public_sdk/blob/586dc5e6c8012c3e4b01c79389375cbe96bdb1da/source/vst/hosting/eventlist.cpp#L65-L73): begrenzte Liste weist weitere Events zurück.
- SDK-Vorschriften, projektspezifische 125A-Erwartungen und optionale Hostfähigkeiten werden getrennt beurteilt. Kein universeller „guter Klang“-Grenzwert wird erfunden.

## 3. Bestandsaufnahme: produktive Architektur und geprüfte Dateien

Die CMake-Einstiegspunkte sind maßgeblich. Ein Dateiname mit „release“ ist allein kein Beweis, dass diese Datei ausgeliefert wird.

| Datei / Komponente | Tatsächlicher Status und statischer Prüfumfang |
| --- | --- |
| CMakeLists.txt, 1–113 | Targets, SDK-Fetch, Plattformzweige, Links, Defines, Fixtures, Installation; Windows-Paket ist der relevante Ablauf. |
| src/portable_stub.cpp, 1–216; portable_stub.rc, 1–33 | Öffentliche GUI-Subsystem-EXE; zehn Hilfs-EXEs aus RCDATA in PID/Tick-Tempverzeichnis extrahieren; GUI oder versteckten portable-test-Modus starten; auf Kind warten; bereinigen. Ressourcen-/Dateifehler und Exitpfade geprüft. |
| src/embedded_helper_ids.h, 1–16 | Ressourcen-IDs 12501–12510; Konsistenz mit Packer-Aufruf und Extraktion geprüft. |
| src/helper_packer.cpp, 1–95; portable_icon_embed.h, 1–87 | Nur Build-/Paketwerkzeug. Dateigrößenlimit, Ressourcen-ID-Prüfung, UpdateResource, Commit/Discard und Icon-Datenaufbau geprüft. Keine neue Paketierung ausgeführt. |
| src/gui_release.cpp, 1–63 | Tatsächliches GUI-Target; Header-Subclass und Theme; bindet gui_final.cpp ein. |
| src/gui_final.cpp, 1–333 | Bindet gui.cpp mit umbenanntem Einstieg ein; tatsächlich verwendeter Einstieg/thematisierter WindowProc; GDI-, Icon-, COM-, DPI- und Fensterlebensdauer geprüft. |
| src/gui.cpp, 1–1074 | Dateidialoge, Bundleauswahl, Workerthread, Prozessstart, Fortschritt, Berichtlesen, Ergebnisdarstellung, Schließen während Test und Ressourcenfreigaben. |
| src/dispatcher_recursive.cpp, 1–240 | Tatsächliches Dispatcher-Target. Bindet dispatcher.cpp ein, ersetzt Einstieg und Ordnersuche. Pfadidentitäten, Junction-/Zyklusvermeidung, serielle Ausführung, Fehlerweitergabe geprüft. |
| src/dispatcher.cpp, 1–635 | Gemeinsam verwendete Prozess-, Status-, JSON-, Summary- und Fortschrittsfunktionen. Alter Einstieg/alte Suche sind im aktiven Target umbenannt beziehungsweise nicht ausgewählt. |
| src/launcher_diagnostics.cpp, 1–561 | Tatsächliches Single-Target; bindet launcher.cpp ein. Validator-Textparser, Normalisierung, Zusatzdiagnose für setProcessing-Rückgaben und deren Berichte geprüft. |
| src/launcher.cpp, 1–776 | Supervisor: Suspended-Start, Job Object, Resume, Zeitlimits, Crash-/Exitcodes; Stufenfolge, Berichtbereinigung und Annotation; eingebundener Legacy-Einstieg bleibt aktive Basisschicht. |
| src/main.cpp, 1–1191 | Worker: Laden, Factory, Component/Controller, Busse, Parameter, State, Konfigurationsmatrix, Audio, Automation, Bypass, Lifecycle, Offline, Restore, Bericht/Exitcode. |
| src/reload_probe.cpp, 1–113 | Fünf serielle vollständige Modul-/Instanzzyklen; Controllererzeugung und Cleanup geprüft. |
| src/state_probe.cpp, 1–331 | Source-Instanz zerstören, neue Destination, State anwenden und erneut speichern; Controllerverbindungen und Warncodes geprüft. |
| src/editor_lifecycle_probe.cpp, 1–467 | COM, Hostfenster, Frame, fünf Editorzyklen, Fokus/Messagepump, Detach/Destroy, StageReport und N/A-Pfade geprüft. |
| src/io_event_probe_logged.cpp, 1–108 | Tatsächlicher I/O-Einstieg; bindet io_event_probe.cpp ein; Wide-Pfad für Logname, Stream-Capture, catch-all und FirstFailure-Bericht geprüft. |
| src/io_event_probe.cpp, 1–662 | Busaktivierung, Arrangement-Roundtrip und Alternativanfrage, Sidechain je Aux, ein Eventblock, Samplepuffer, Lifecycle, Ausgabevalidierung und Cleanup. |
| src/validator_wrapper.cpp, 1–225 | Core-Start und Argumentquotierung, vererbte Log-/NUL-Handles, Wait, Normalisierung von -1, Erhaltung NT-Exception-Codes; Timeout kommt vom äußeren Job-Supervisor. |
| src/report_paths.h, 1–131 | Report-Root-Override, UTF-Konvertierung, absolute/lowercase lexikalische Identität, FNV-1a-Pfadhash, alle Reportdateinamen. |
| src/guard_selftest.cpp, 1–47 | Interner Helfer für ok/fail/crash/hang; keine Enkelprozess- oder Elternabbruchkontrolle. |
| tests/qa_fixture.cpp, 1–295 | Sieben Compilevarianten: GoodControl, BadLifecycle, BadNaN, BadState, BadIO, GoodEditor, BadEditor. Genaues Fehlerverhalten geprüft. |
| .github/workflows/build-windows.yml, 1–242 | Tatsächliche Targetliste, Guard-/Validator-/Fixture-Prüfungen, Ordnerscan, Ressourcenpaketierung, portable E2E und Uploadbedingungen vollständig gelesen. Ausführung nicht belegt. |
| src/launcher_release.cpp, 1–90 | Nicht als CMake-Target eingebunden. Alternative I/O-Detailannotation; nicht mit der tatsächlich gebauten diagnostics-Variante verwechselt. |
| src/dispatcher_release.cpp, 1 | Nicht als eigenes Target eingebunden; lediglich Include von dispatcher_recursive.cpp. |
| AGENTS.md, CURRENT.md, README.md, LICENSE-NOTE.txt | Navigation, belegte/behauptete Versionen, Funktionsversprechen, Paketbeschreibung und Lizenzhinweis geprüft. |

Drittanbieter-SDK: relevante Header, Hosting-Klassen, EventList und Submodulrevisionen wurden geprüft. Kein vollständiges unabhängiges Audit aller Drittanbieter-SDK-Quellen oder der tatsächlichen kompilierten Validator-EXE.

### 3.1 Aktive Prozesskette

Öffentliche portable EXE → GUI → Dispatcher → Single-Launcher → nacheinander Reload, State, Editor, I/O/Event, Validator-Wrapper → Validator-Core und Worker. Bei bestimmtem Workerfehler startet Single zusätzlich eine isolierte setProcessing-Diagnose.

Der Stufen-Supervisor besitzt pro Stufe ein nicht vererbtes Job-Handle und startet suspendiert vor Jobzuweisung. Dadurch wird das anfängliche Start-/Zuordnungsrennen vermieden; Core und reguläre Nachfahren der Stufe befinden sich normalerweise ebenfalls im Job. Das ist Absturz-/Hangbegrenzung, **keine Sicherheits-Sandbox für bösartige Plugins**.

GUI/Dispatcher/Portable besitzen dagegen keine gemeinsame äußere Jobüberwachung. Die GUI wartet in einem abgetrennten Thread; normale WM_CLOSE-Schließung wird während eines Tests abgelehnt. Ein erzwungener Prozessabbruch ist ein anderer Fall (TESTER-012).

### 3.2 Vorhandene positive Eigenschaften und Grenzen

- Korrekte Windows-Argumentquotierung einschließlich abschließender Backslashes ist in den Startfunktionen erkennbar; Unicode-Konvertierung in schmalen Hilfsprogrammen bleibt getrennt offen.
- Teilerfolgreiche IConnectionPoint-Verbindungen werden wieder getrennt; die wesentlichen normalen Pfade geben Interfaces, Prozesshandles und Views frei.
- Bus-Layoutänderungen nach nicht erfolgreichem setBusArrangements werden nicht automatisch als unverändert angenommen; tatsächliche Arrangements werden gelesen und wiederhergestellt.
- Optionale abgelehnte Layout-/Aktivierungsanfragen werden nicht pauschal als Pluginfehler behandelt.
- Validator -1 wird im Wrapper von echten NT-Exceptions getrennt. Sein unendlicher eigener Wait ist im normalen Produktionsaufruf vom äußeren Timeout umfasst; alleine gestartet gilt diese Grenze nicht.
- Fortschritt benutzt eine temporäre Datei plus MoveFileEx; andere Reports werden überwiegend direkt überschrieben.
- GUI/GDI-Ressourcen werden in normalen Pfaden freigegeben; COM wird bei erfolgreicher Initialisierung beendet. Kein pauschaler Leak-/Race-/Deadlock-Freiheitsnachweis.
- Keine projektseitige Netzwerk-/Telemetrie- oder Credential-Übertragung in den gelesenen Produktionspfaden gefunden. Ein geladenes Drittanbieterplugin behält seine eigenen Rechte.
- Gefundene schwache Fehlerpfade werden unten dokumentiert; darüber hinausgehende Fehlerfreiheit wird nicht behauptet.

## 4. CI, Releases, Artefakte und tatsächlich ausgeführte Prüfungen

### 4.1 Was der Workflow implementiert — keine Aussage über erfolgte Läufe

1. Windows-x64-Release-Konfiguration und Build von Produktion, Packer, Guard-Helfer und sieben Fixtures.
2. Vier Guard-Fälle über den Produktionssupervisor: 0, 1, Crash→126, Hang→124.
3. Offizieller Validator-Selbsttest über Wrapper.
4. Sieben Fixture-Aufrufe mit erwartetem Exit/JSON-Status/TXT-/I/O-/Editorbeleg. BadEditor verursacht absichtlich beim zweiten removed einen Absturz.
5. Ordnerscan mit zwei Fixtures: ein PASS und ein FAIL; Aggregate, Fortschritt und zwei Einzel-JSONs.
6. Einbetten zehn interner EXEs in eine öffentliche portable EXE.
7. Portable E2E: gutes Bundle, gute standalone .vst3, BadIO; Start-Process -Wait -PassThru berücksichtigt GUI-Subsystem.
8. Genau eine öffentliche EXE; interne Fixtures dürfen nicht im Artefakt liegen; Fehlerdiagnosen und Paket als Actions-Artefakte hochladen.

Fundstellen: [.github/workflows/build-windows.yml:16–151](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/.github/workflows/build-windows.yml#L16-L151), [.github/workflows/build-windows.yml:153–242](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/.github/workflows/build-windows.yml#L153-L242).

**README-Abweichungen:** „nur manuell“ ist falsch, da zusätzlich push auf main aktiviert ist. Sieben lose EXEs/vier Fixtures entsprechen nicht dem aktuellen Single-EXE-Paket mit zehn eingebetteten Helfern/sieben Fixtures. JSON und GUI sind bereits implementiert, obwohl als spätere Erweiterung erwähnt. Guardversionen sind zusätzlich veraltet (TESTER-004). Parser-Selbsttest ist implementiert, wird im aktuellen Workflow aber nicht aufgerufen.

### 4.2 Historischer Beweisstatus der Erstprüfung (aktuelle Ergänzung: Abschnitt 12)

| Evidenzebene | Tatsächlich vorhanden / ausgeführt | Aussagegrenze |
| --- | --- | --- |
| Statische Projektanalyse | Komponenten-/Aufrufkettenprüfung, Manifest-/Workflow-Abgleich, SDK-Vertragsprüfung durchgeführt | Keine Laufzeitmessung |
| Git-Baseline | ls-remote main/HEAD; lokale Historie; 26 technische Dateien byteweise mit fd24614 verglichen | Git-Leserecht ist kein API-/Schreibrecht |
| Vorhandene Unit-/Regressionsergebnisse | **NOT VERIFIED** | Workflow-Text wurde gelesen, keine erfolgreichen Run-Logs verfügbar |
| SDK-Validator-Läufe/-Selbsttest | **NOT VERIFIED** | Nicht auf Windows gestartet |
| Windows-Runtime | **NOT VERIFIED** | Kein geeigneter Windows-Host in dieser Sitzung |
| Audio-/MIDI-Funktionsmessungen | **NOT VERIFIED** | Keine Plugin-Renderings oder tatsächlichen Eventmessungen durchgeführt |
| Studio One / andere DAW | **NOT VERIFIED** | Keine Host-/Bedienprüfung |
| Aktuelles Windows-Paket / Prüfsumme | **NOT VERIFIED** | Keine konkrete ZIP/EXE zugeordnet |
| GitHub-CI-/Release-Abfrage | gh api actions/runs und releases scheiterten mit Forbidden; curl meldete CONNECT tunnel failed, response 403 | Proxy-/Zugangsblocker; kein Beleg für fehlende oder fehlgeschlagene Builds |
| Frühere Linux-Onboarding-Prüfung | CMake-Konfiguration erfolgreich; Workerbuild scheiterte an windows.h in report_paths.h | Kontext aus Einrichtung, kein Windows-Defekt und keine Windows-QA; nicht erneut gebaut |

Keine Tests wurden in dieser Auditphase neu kompiliert oder ausgeführt, die technische Repository-Dateien verändern oder neue Artefakte erzeugen. Reproduktionsfälle in den Befunden sind **vorgeschlagene zukünftige Tests**, sofern nicht anders gekennzeichnet. Keine Befundklasse RUNTIME CONFIRMED.

Keine neuen GitHub-Actions-Läufe, Workflows, Releases oder Pakete erzeugt. Lokale Tags enthalten keinen v0.2.9-Nachweis; die Remote-Tagabfrage lieferte keine Tags. Das sagt nichts über möglicherweise vorhandene Actions-Artefakte oder ein externes Nutzerpaket aus.

### 4.3 Für eine belastbare Paketzuordnung benötigt

Konkrete Paketdatei und SHA-256, SHA-256 der öffentlichen EXE, zugehöriger Actions-Run samt head_sha, Job-/Schrittlogs und Artefakt-ID/-Digest; gegebenenfalls Erklärung des externen ZIP-Aufbaus. Ein erfolgreicher Run eines anderen 125A-Plugins beweist nicht die Herkunft dieser Tester-EXE. Die letzte technisch ändernde Quelle fd24614 ist ein Kandidat, **kein nachweislich funktionierender Build**.

## 5. Befundregister

Kategorien: BESTÄTIGTER FEHLER / MÖGLICHER FEHLER / OPTIMIERUNG. Nachweisklassen: CODE CONFIRMED / RUNTIME CONFIRMED / SUSPECTED / ENHANCEMENT. Status OPEN bedeutet dokumentiert, nicht zur Reparatur freigegeben. APPROVED darf erst nach ausdrücklicher Entscheidung gesetzt werden; FIXED erst mit Änderungs- und Testbeleg.

### TESTER-001 — Kanalzeiger-Array inaktiver Audiobusse fehlt

**Kategorie:** BESTÄTIGTER FEHLER; **Evidenz:** CODE CONFIRMED; **Schweregrad:** HIGH; **Status:** OPEN.

**Fundstellen:** [src/main.cpp:381–403](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/main.cpp#L381-L403), [src/io_event_probe.cpp:297–321](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/io_event_probe.cpp#L297-L321); SDK-A.

**Beschreibung/Nachweis:** numChannels wird aus BusInfo gesetzt. channels und Zeiger-Array werden nur bei active angelegt. Beim inaktiven Stereo-Bus bleibt numChannels=2, channelBuffers aber nullptr. SDK-A verlangt ein Array der Kanalzahl auch bei inaktivem Bus; nur dessen Samplezeiger dürfen dann null sein. Die Busreihenfolge wird zwar erhalten, der vollständige Buffervertrag nicht.

**Bedingung/Auswirkung:** mindestens ein inaktiver Bus mit Kanälen. Ungültige Hostdaten sind bewiesen; ein tatsächlicher Plugincrash oder False Positive ist nicht reproduziert. Beide Sampleformate betroffen. Worker orientiert sich außerdem an DefaultActive statt bestätigter tatsächlicher Aktivierung.

**Vorschlag/Risiko:** getrennte Allokation der Kanalzeiger und aktiven Samplepuffer; bestätigten Aktivierungszustand führen. Risiko MEDIUM: gemeinsame Pufferverwaltung und Aux-Routing.

**Regression:** korrekte Fixtures mit inaktivem Eingangs-/Ausgangsbus vor/nach aktiven Bussen, jeweils 32/64 Bit; Arraylänge, Null-Samplezeiger, Reihenfolge und unveränderte Pluginergebnisse prüfen. **Offen:** Windows-Reproduktion, betroffene reale Plugins.

### TESTER-002 — Bericht überstimmt widersprechenden Prozessausgang

**Kategorie:** BESTÄTIGTER FEHLER; **Evidenz:** CODE CONFIRMED; **Schweregrad:** HIGH; **Status:** OPEN.

**Fundstellen:** [src/dispatcher.cpp:271–306](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/dispatcher.cpp#L271-L306), [src/dispatcher_recursive.cpp:148–174](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/dispatcher_recursive.cpp#L148-L174).

**Beschreibung/Nachweis:** Ohne Guard wird ein lesbarer QA-RESULT-Zähler vor dem Exitcode ausgewertet. Bei fail=0 und warning=0 folgt PASS auch für exitCode=1/124/126. Die parsende sscanf-Abfrage prüft weder Vollständigkeit/Abschluss noch plausible nichtnegative Gesamtzähler. Ein auf eine RESULT-Zeile reduzierter Bericht reicht. Bei Ordnerscans basiert der Gesamtexit auf den so ermittelten Statuswerten.

**Bedingung/Auswirkung:** widersprechender, nach frühem Abbruch verbliebener oder beschädigter Bericht ohne wirksamen Guard. Status/JSON können PASS melden; der Einzelaufruf kann daneben weiterhin einen Fehler-Exitcode liefern. Nicht behauptet wird, jeder Crash erzeuge diese Kombination.

**Vorschlag/Risiko:** Abschluss, Run-ID, Stagevollständigkeit, plausible Zähler und Exitcode gemeinsam prüfen; Widersprüche INCONCLUSIVE. Risiko MEDIUM: Status-/Exitcode-Kompatibilität.

**Regression:** Kombinationstabelle für exit 0/1/2/124/125/126/127, fehlenden/abgeschnittenen/alten Bericht, Guard, negative Zähler und fehlende Pflichtstufen. Kein widersprüchlicher Fall darf PASS werden. **Offen:** Windows-E2E-Fehlerinjektion.

### TESTER-003 — Berechtigungsbedingt ausgelassene Scanbereiche bleiben unsichtbar

**Kategorie:** BESTÄTIGTER FEHLER; **Evidenz:** CODE CONFIRMED; **Schweregrad:** MEDIUM; **Status:** OPEN.

**Fundstellen:** [src/dispatcher_recursive.cpp:61–94](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/dispatcher_recursive.cpp#L61-L94), [src/dispatcher_recursive.cpp:158–174](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/dispatcher_recursive.cpp#L158-L174).

**Beschreibung/Nachweis:** skip_permission_denied kann Verzeichnisse ohne Fehler überspringen; zusätzlich wird permission_denied beim Erfassen von Fehlern ausgenommen. Damit ist discovery.errors kein vollständiger Nachweis der Scanvollständigkeit.

**Bedingung/Auswirkung:** ein lesbares Plugin und ein unzugänglicher Unterbaum. Der unzugängliche Bereich kann ohne Vollständigkeitswarnung fehlen. Anzahl tatsächlich übersehener Plugins unbekannt.

**Vorschlag/Risiko:** explizite Erfassung nicht untersuchter Bereiche und separater Vollständigkeitsstatus. Risiko LOW–MEDIUM: Netzwerkpfade/Junctions/Zyklen beachten.

**Regression:** Windows-ACL-Test mit zugänglichem Plugin, gesperrtem Unterbaum und Junctionzyklus; vollständige Prüfung darf nicht behauptet werden. **Offen:** Windows-ACL-Reproduktion.

### TESTER-004 — Widersprüchliche Versionen und veraltete Betriebsbeschreibung

**Kategorie:** BESTÄTIGTER FEHLER; **Evidenz:** CODE CONFIRMED; **Schweregrad:** LOW; **Status:** OPEN.

**Fundstellen:** [src/portable_stub.rc:3–26](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/portable_stub.rc#L3-L26), [src/launcher.cpp:231–237](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/launcher.cpp#L231-L237), [src/launcher.cpp:518–521](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/launcher.cpp#L518-L521), [CMakeLists.txt:2](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/CMakeLists.txt#L2), README.md Abschnitte Crash/hang isolation, Tester self-validation, Build und Later extensions; Workflow [.github/workflows/build-windows.yml:3–6](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/.github/workflows/build-windows.yml#L3-L6).

**Beschreibung/Nachweis:** öffentliche PE-Ressource 0.2.0, eingebundener Launcher/Guard 0.2.5, Worker/JSON/Projekt 0.2.9. README beschreibt vier statt sieben Fixtures, sieben lose EXEs statt aktueller Portable-Kette, rein manuelle CI trotz push-main und bereits vorhandene GUI/JSON als Zukunft.

**Auswirkung:** falsche Identifizierung und Bedien-/QA-Erwartungen; kein Nachweis falscher DSP-Funktion. **Vorschlag/Risiko:** eine Versionsquelle, erzeugte Metadaten und zielgenaue Dokumentationskorrektur; LOW. Keine Versionsanhebung vorgeschlagen.

**Regression:** PE-Version, alle Berichte/JSON und Banner vergleichen; README-Workflow-/Target-Abgleich. **Offen:** tatsächliche Metadaten des Nutzerpakets.

### TESTER-005 — Verpflichtender IComponentHandler fehlt

**Kategorie:** BESTÄTIGTER FEHLER; **Evidenz:** CODE CONFIRMED; **Schweregrad:** HIGH; **Status:** OPEN.

**Fundstellen:** [src/main.cpp:1090–1139](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/main.cpp#L1090-L1139), [src/state_probe.cpp:45–111](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/state_probe.cpp#L45-L111), [src/editor_lifecycle_probe.cpp:129–184](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/editor_lifecycle_probe.cpp#L129-L184), [src/launcher_diagnostics.cpp:268–314](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/launcher_diagnostics.cpp#L268-L314); SDK-B. Repositoryweite Suche in src/ findet keinen setComponentHandler-Aufruf.

**Beschreibung/Nachweis:** Controller werden erzeugt, initialisiert und teilweise verbunden; der verpflichtende Hosthandler wird nicht gesetzt. HostApplication ersetzt dies nicht: die SDK-Klasse implementiert IHostApplication, nicht automatisch die separate Controller-Handler-Bindung.

**Bedingung/Auswirkung:** Verwendung eines Controllers, insbesondere dessen GUI-/Edit-/restartComponent-Wege. Vertragslücke ist nachgewiesen; konkrete Abstürze oder Fehlbewertungen sind NOT VERIFIED. Parameteränderungs-, Dirty- und Neustartbenachrichtigungen können nicht realistisch geprüft werden.

**Vorschlag/Risiko:** minimalen vertragsgemäßen Handler mit expliziter Lebensdauer, Threadzuordnung und begrenzter Callbackverarbeitung bereitstellen; keine pauschale Unterstützung aller optionalen Handler-Erweiterungen vortäuschen. Risiko MEDIUM–HIGH: Hostintegration/GUI.

**Regression:** korrektes Fixture, das Handlerbindung vor createView erwartet; begin/perform/endEdit, restartComponent und sauberes Abmelden kontrollieren, positive/negative Fälle trennen. **Offen:** reale Pluginreaktionen und Windows-Threadprüfung.

### TESTER-006 — GUI zeigt unvollständige Ergebnisse zu positiv an

**Kategorie:** BESTÄTIGTER FEHLER; **Evidenz:** CODE CONFIRMED; **Schweregrad:** HIGH; **Status:** OPEN.

**Fundstellen:** [src/gui.cpp:496–553](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/gui.cpp#L496-L553), [src/gui.cpp:693–715](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/gui.cpp#L693-L715), [src/gui.cpp:847–902](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/gui.cpp#L847-L902).

**Beschreibung/Nachweis:** Ordnerstatus prüft FAIL/INCONCLUSIVE/WARNING-Zähler, ignoriert aber DISCOVERY_ERRORS und bei Nullzählern den Fehler-Exitcode. Ein Summary mit 1 Discovery Error und sonst nur PASS wird GUI-PASS. Bei Einzelprüfungen bevorzugt resultTextFor einen QA-Bericht vor einem vorhandenen Guard. addPlainLanguageSummary setzt ohne FAIL/WARN-Zeile stets eine PASS-Kurzerklärung, auch bei fehlendem oder unlesbarem Bericht. Dadurch kann etwa oben INCONCLUSIVE stehen, während die Tabelle PASS erklärt.

**Auswirkung:** irreführende sichtbare Bewertung; unabhängig von der Dispatcherlogik TESTER-002. **Vorschlag/Risiko:** eine validierte Ergebnisquelle mit expliziter Vollständigkeit, Guardpriorität und Fehlerzuständen für alle UI-Flächen; MEDIUM.

**Regression:** fehlender Bericht, Exit 127, alter QA plus neuer Guard, Summary mit Discovery Error und defektes UTF-8; Statusfarbe, Kurztext und Tabelle müssen konsistent unvollständig/fehlerhaft ausweisen. **Offen:** tatsächliche Screenshots/Windows-Bedienprüfung.

### TESTER-007 — Bericht-/Annotationsfehler werden nicht zuverlässig zum Laufstatus

**Kategorie:** BESTÄTIGTER FEHLER; **Evidenz:** CODE CONFIRMED; **Schweregrad:** HIGH; **Status:** OPEN.

**Fundstellen:** [src/main.cpp:62–75](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/main.cpp#L62-L75), [src/main.cpp:1179–1190](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/main.cpp#L1179-L1190), [src/launcher.cpp:710–772](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/launcher.cpp#L710-L772), [src/dispatcher_recursive.cpp:115–120](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/dispatcher_recursive.cpp#L115-L120), [src/dispatcher_recursive.cpp:151–174](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/dispatcher_recursive.cpp#L151-L174), [src/io_event_probe_logged.cpp:55–75](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/io_event_probe_logged.cpp#L55-L75), [src/editor_lifecycle_probe.cpp:187–214](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/editor_lifecycle_probe.cpp#L187-L214).

**Beschreibung/Nachweis:** Reporter::write gibt nach Ausgabe ungeprüft true zurück; spätere Schreib-/Flushfehler bleiben unberücksichtigt. Fehlgeschlagene Stufenannotation wird nur auf stderr gemeldet. Dispatcher kann JSON-/Summary-Schreibfehler melden und trotzdem 0 zurückgeben; Fortschritts-Rückgaben und Löschfehler der Scanreports werden ignoriert. I/O-/Editor-Sidecars können still fehlen. Einzel-JSON gehört nicht zur Stale-Bereinigung des Single-Launchers.

**Bedingung/Auswirkung:** Reportdatei gesperrt, Volume voll, ungültige Berechtigung oder I/O-Ausfall. Vollständigkeit/Status können auseinanderfallen; bei fehlender FAIL-Annotation kann TESTER-002 zusätzlich greifen. Keine tatsächliche Datenträgerstörung reproduziert.

**Vorschlag/Risiko:** Ergebnisse strukturiert sammeln, erforderliche Reports atomar mit geprüftem Flush/Close abschließen, Fehler als Infrastruktur/INCONCLUSIVE weitergeben; alte Dateien eindeutig isolieren. Risiko MEDIUM: nicht jeden optionalen Komfortbericht zum Plugin-FAIL machen.

**Regression:** Fehler beim Öffnen, Schreiben, Flush, Annotation, Rename und Stale-Löschen; weder alter Report noch fehlende Pflichtstufe darf einen neuen erfolgreichen Lauf belegen. **Offen:** Windows-Fault-Injection, Definition verpflichtender versus optionaler Dateien.

### TESTER-008 — Latenz und Tail vor gültigem Setup abgefragt

**Kategorie:** BESTÄTIGTER FEHLER; **Evidenz:** CODE CONFIRMED; **Schweregrad:** MEDIUM; **Status:** OPEN.

**Fundstelle:** [src/main.cpp:1141–1152](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/main.cpp#L1141-L1152); SDK-C.

**Beschreibung/Nachweis:** getLatencySamples/getTailSamples werden vor inspectProcessingSetups beziehungsweise dem ersten setupProcessing aufgerufen. Beide SDK-Methoden nennen Setup Done als Zustand. Sample-Rate-abhängige Angaben können so außerhalb ihrer dokumentierten Phase abgefragt werden.

**Auswirkung:** unbrauchbare Latenz-/Tailinformation und unzulässige Hostreihenfolge; kein nachgewiesener Crash. **Vorschlag/Risiko:** nach erfolgreichem Setup abfragen, Format/Rate/Modus dazuschreiben; LOW–MEDIUM.

**Regression:** korrektes Fixture mit erst nach Setup gültiger rateabhängiger Latenz/Tail und separaten Restart-/Änderungsfällen. **Offen:** Verhalten des Nutzerpakets mit solchen Plugins.

### TESTER-009 — Controller-Erzeugungsfehler werden in Einzelproben verschluckt

**Kategorie:** BESTÄTIGTER FEHLER; **Evidenz:** CODE CONFIRMED; **Schweregrad:** MEDIUM; **Status:** OPEN.

**Fundstellen:** [src/reload_probe.cpp:48–87](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/reload_probe.cpp#L48-L87), [src/editor_lifecycle_probe.cpp:140–157](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/editor_lifecycle_probe.cpp#L140-L157), [src/editor_lifecycle_probe.cpp:269–276](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/editor_lifecycle_probe.cpp#L269-L276), [src/editor_lifecycle_probe.cpp:428–441](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/editor_lifecycle_probe.cpp#L428-L441).

**Beschreibung/Nachweis:** Reload kennt eine Controller-CID, aber eine fehlgeschlagene Factory-Erzeugung führt nicht zu einem negativen Ergebnis; trotzdem wird vollständiger Component/Controller-Lifecycle als PASS beschrieben. Im Editor-Probe werden fehlende optionale Controller und Fehler bei Erzeugung/Initialisierung gemeinsam als „no usable controller“ übersprungen; bei fehlenden Editoren folgt N/A. Bei mehreren Klassen kann trotz übersprungener defekter Klasse PASS folgen.

**Auswirkung:** einzelne Stufenergebnisse können unberechtigten PASS/N/A enthalten. Der spätere Worker prüft Controllerfehler teilweise strenger; daher nicht behaupten, jeder solche Fall bestehe die ganze Kette.

**Vorschlag/Risiko:** „nicht vorhanden“ von „vorhanden, aber fehlgeschlagen“ unterscheiden und pro Klasse berichten; LOW–MEDIUM.

**Regression:** gültige CID mit fehlgeschlagener Erzeugung/Initialisierung, echte controllerlose Klasse, Single-Component-Controller und Mehrklassenmodul. **Offen:** Windows-E2E und gewünschte optionale Controllerpolitik.

### TESTER-010 — Kodierung schmaler Windows-Argumente

**Kategorie:** MÖGLICHER FEHLER; **Evidenz:** SUSPECTED; **Schweregrad:** MEDIUM; **Status:** OPEN.

**Fundstellen:** [src/reload_probe.cpp:93–103](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/reload_probe.cpp#L93-L103), [src/state_probe.cpp:270–284](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/state_probe.cpp#L270-L284), [src/main.cpp:1009–1035](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/main.cpp#L1009-L1035), [src/editor_lifecycle_probe.cpp:446–455](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/editor_lifecycle_probe.cpp#L446-L455), [src/io_event_probe_logged.cpp:80–99](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/io_event_probe_logged.cpp#L80-L99), [src/io_event_probe.cpp:598–605](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/io_event_probe.cpp#L598-L605), [CMakeLists.txt:6–8](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/CMakeLists.txt#L6-L8).

**Verdacht/Nachweisgrenze:** CreateProcessW übergibt Unicode, mehrere Helfer lesen char** argv als UTF-8. /utf-8 ist eine Quell-/Ausführungsliteraloption, keine Garantie einer UTF-8-CRT-Kommandozeile. Der I/O-Wrapper liest zwar für den Lognamen den Wide-Pfad, reicht aber die ursprünglichen schmalen Argumente an den eigentlichen Probe weiter.

**Bedingung/Auswirkung:** Windows mit nicht UTF-8-Systemcodepage und Nicht-ASCII-Pfaden; falsche Modul-/Reportpfade möglich. Auf UTF-8-konfiguriertem System möglicherweise nicht reproduzierbar. **Vorschlag/Risiko:** einheitlicher Wide-Einstieg mit expliziter UTF-8-Konvertierung nur für SDK-Strings; MEDIUM.

**Regression/Offen:** Paket auf üblicher Legacy-Codepage und UTF-8-System testen, Benutzer-/TEMP-/Plugin-/Reportpfade mit ä, ß, CJK und Supplementärzeichen, Bundle/Standalone, Leerzeichen und Backslashes. Erst danach RUNTIME CONFIRMED oder begründete Zurückweisung.

### TESTER-011 — Gleichzeitige Läufe können Reports gegenseitig überschreiben

**Kategorie:** MÖGLICHER FEHLER; **Evidenz:** SUSPECTED; **Schweregrad:** HIGH; **Status:** OPEN.

**Fundstellen:** [src/report_paths.h:63–129](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/report_paths.h#L63-L129), [src/launcher.cpp:554–562](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/launcher.cpp#L554-L562), [src/dispatcher.cpp:394–427](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/dispatcher.cpp#L394-L427), [src/dispatcher_recursive.cpp:115–120](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/dispatcher_recursive.cpp#L115-L120).

**Verdacht/Nachweisgrenze:** Pfadhash trennt gleichnamige Plugins an verschiedenen Pfaden, aber keine zwei Läufe desselben Pfads. Ordnersummary und Fortschritt haben sogar globale feste Namen und denselben .tmp-Namen. Kein prozessübergreifendes Lock oder Run-ID-Verzeichnis gefunden. Damit sind die gemeinsamen Ziele codebestätigt; ein tatsächlich falsches Ergebnis durch eine bestimmte Interleavingfolge ist noch nicht reproduziert.

**Auswirkung:** Löschung/Überschreibung fremder laufender Reports, falscher Fortschritt oder fremder Status. Einzelne GUI verhindert nur ihren eigenen zweiten Start.

**Vorschlag/Risiko:** entweder explizite Single-Run-Sperre pro Report-Root oder Run-ID-Verzeichnisse und atomare Veröffentlichung; MEDIUM. Einfachere Sperre zuerst bewerten.

**Regression/Offen:** zwei GUI-/CLI-Läufe, gleiche und verschiedene Ziele, parallele Ordnerscans und Prozessabbruch während Veröffentlichung; Zuordnung jedes Ergebnisses prüfen.

### TESTER-012 — Äußerer Prozessabbruch kann die Testkette zurücklassen

**Kategorie:** MÖGLICHER FEHLER; **Evidenz:** SUSPECTED; **Schweregrad:** MEDIUM; **Status:** OPEN.

**Fundstellen:** [src/portable_stub.cpp:129–163](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/portable_stub.cpp#L129-L163), [src/gui.cpp:469–493](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/gui.cpp#L469-L493), [src/gui.cpp:839–844](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/gui.cpp#L839-L844), [src/gui.cpp:993–1007](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/gui.cpp#L993-L1007), [src/dispatcher.cpp:114–182](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/dispatcher.cpp#L114-L182), [src/launcher.cpp:319–444](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/launcher.cpp#L319-L444).

**Verdacht/Nachweisgrenze:** Nur Single→Stufe ist mit Job Objects überwacht. GUI und Portable warten unendlich auf Kinder; Dispatcher beendet bei eigenem Timeout nur Single. Normales GUI-Schließen ist gesperrt, aber ein Task-Manager-Abbruch von GUI/Portable kann Dispatcher/Single ohne äußeren Besitzer zurücklassen. Stirbt Single, wird sein Stufenjob üblicherweise geschlossen; das beseitigt nicht automatisch jeden äußeren Vorfahren.

**Auswirkung:** verbleibende Testprozesse/Tempdateien oder Bereinigung während noch laufender Helfer möglich. Keine Prozessbaum-Leak-Reproduktion.

**Vorschlag/Risiko:** explizite äußere Ownership/Abbruchkette und Wiederanlaufbereinigung; MEDIUM–HIGH, bestehende sichere innere Jobs nicht ersetzen.

**Regression/Offen:** Eltern an jeder Stufe beenden, Enkelprozess absichtlich erzeugen, Ende aller zur Sitzung gehörenden Prozesse und ausschließlich eigener Tempdaten prüfen. Kein pauschaler Leakvorwurf für normale Exitpfade.

### TESTER-013 — Widersprüchliche Behandlung host-spezifischer Mix-FX-Klassen

**Kategorie:** MÖGLICHER FEHLER; **Evidenz:** SUSPECTED; **Schweregrad:** MEDIUM; **Status:** OPEN.

**Fundstellen:** [src/main.cpp:1147–1152](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/main.cpp#L1147-L1152), [src/io_event_probe.cpp:616–647](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/io_event_probe.cpp#L616-L647), [src/state_probe.cpp:300–306](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/state_probe.cpp#L300-L306), [src/reload_probe.cpp:36–39](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/reload_probe.cpp#L36-L39).

**Verdacht/Nachweisgrenze:** Worker behandelt „Audio Mix Processor“ als host-spezifisch und überspringt Standardverarbeitung. I/O-Probe nimmt dieselbe Kategorie an und führt Standardsetup/-verarbeitung aus, wenn Aux-/Eventtests anwendbar sind. Ob die konkrete proprietäre Klasse diese Aufrufe unterstützen muss, ist aus dem allgemeinen VST3-Vertrag allein nicht ableitbar.

**Auswirkung:** host-spezifische Klassen könnten je nach Stufe widersprüchlich oder falsch bewertet werden.

**Vorschlag/Risiko:** unterstützte Klassen/Hostfähigkeiten konsistent deklarieren, ununterstützte Prüfungen als solche ausweisen; MEDIUM.

**Regression/Offen:** tatsächliche Mix-FX-Schnittstellenspezifikation und geeignete positive Kontrolle beschaffen; keine generische Ausnahme auf Standard-AudioEffect-Klassen ausdehnen.

## 6. Testabdeckungsmatrix

JA = implementierter Codepfad, nicht automatisch ausgeführter oder bestandener Test. Positiv/Negativ bezieht sich auf eigene Tester-Fixtures beziehungsweise Selbsttests, nicht auf vermutete Abdeckung durch den offiziellen Validator. P0 = einfaches GoodControl; PL/PN/PS/PI/PE = BadLifecycle/BadNaN/BadState/BadIO/BadEditor; PG = GoodEditor. Alle Windows-Ergebnisse dieser Kontrollen bleiben NOT VERIFIED.

| Testbereich | Implementiert | Tatsächlich geprüftes Verhalten | Fehlende Prüfung | Positive Kontrolle | Negative Kontrolle | Risiko / Empfehlung |
| --- | --- | --- | --- | --- | --- | --- |
| Modul laden/Factory/Klassen | JA | Load, Aufzählung, unterstützte Kategorien | Defekte Factory, Mehrklassen, Kardinalität gezielt | P0, PG einfach | kein gezielter Load-Fall | Mittel; Mehrklassenkontrolle |
| Modulreload | JA | 5 serielle Modul-/Instanzzyklen | echte Reloadfehler, Controller-Creation-Fail, Ressourcenzählung | P0 indirekt | kein Reload-Fixture | TESTER-009 |
| Component init/terminate | JA | Rückgaben in mehreren Probes | alle Cleanupfehler/Abbruchphasen | P0 | PL betrifft setProcessing | Mittel; gezielte Lifecyclevarianten |
| Single-/Separate-Controller | TEILWEISE | QI, CID, create/init; Verbindungen | Handler; konsistente Fehlereinstufung | P0 separat | kein Handler-/CID-Fehler | TESTER-005/009 |
| IConnectionPoint | TEILWEISE | bidirektional, Teilrollback | Inhalt/Reihenfolge von Nachrichten | P0 SDK-Basis | keine gezielte | Hostprofil statt blindes Pflichtfeature |
| Busmetadaten/-reihenfolge | TEILWEISE | Zähler, Kanalzahlen, Descriptorreihenfolge | valide inaktive Pointerarrays | P0 nur Stereo-main | PI nur Arrangementindex | TESTER-001 |
| Aktivieren/deaktivieren | JA | reversible Anfragen; optionale Ablehnung toleriert | tatsächliche Aktivierungszustände breit | P0 einfach | kein gezielter Übergangsfehler | Mittel; Aux-/inaktive Kontrollen |
| Arrangement/Mono/Stereo | TEILWEISE | Istlayout, ungültiger oberer Index, ein alternatives Layout und Restore | Verarbeitung im Alternativlayout; mehrere Layouts | P0 einfach | PI | echte Monoverarbeitung ergänzen |
| Mehrkanal/Aux/Sidechain | TEILWEISE | Aux einzeln aktivieren, 1 Block Impuls, endlich | Nebenbuswirkung, Multioutput, Kanalisolation | keine Aux-Fixture | keine | TESTER-020/016 |
| 32-/64-Bit-Audio | TEILWEISE | Muster und Sustained in beiden unterstützten Formaten | Automation/Bypass/Lifecycle/Offline meist nur bevorzugtes Format | P0 nur 32 Bit | PN nur 32 Bit erreichbar; 64-Bit-Code nicht angekündigt | TESTER-015 |
| Sample-Rate-Matrix | TEILWEISE | setupProcessing bei 44.1/48/88.2/96/192 kHz | process pro Kombination | P0 Setup | keine rateabhängige | TESTER-015 |
| Blockgrößenmatrix | TEILWEISE | maxSamplesPerBlock 1/32/64/128/256/512/1024/2048/4096/8192 | tatsächliche wechselnde numSamples, Nullflush | P0 Setup | keine blockabhängige | TESTER-015 |
| Stille/Impuls/DC/Denormalinput | JA | je 512 Samples, endlich; Peak/RMS/DC/Subnormalzählung | AC/Noise/Sweep, Verlauf, geeignete Nutzungssignale | P0 gibt immer Stille aus | PN NaN | Zahlen nicht als Klangqualität interpretieren |
| NaN/Infinity im Output | JA | isfinite über allokierte Ausgabepuffer | Infinity-eigene Fixture, speicherüberschreibende Fälle | P0 | PN nur NaN | kleine Zusatzkontrolle |
| Subnormals | TEILWEISE | native float/double-Klassifikation, Anzahl in Detail | Performanceeffekt, FTZ/DAZ-Metadaten | keine spezifische | keine | Information, kein pauschaler FAIL |
| Extremwerte | TEILWEISE | DC 0.5, Impuls 1, Param 0/0.5/1 | breite gültige Audiosweeps und Kombinationen | P0 eingeschränkt | keine | profilabhängig; kein Host mit NaN füttern und generisch FAIL verlangen |
| Sustained-Verarbeitung | JA | 256×512 Samples bei 48 kHz, stiller Input | lange Decays, nichtstille Kontinuität | P0 | PN kann früh auffallen | TESTER-019 |
| Wiederholter Lifecycle | TEILWEISE | 8 active/process/deactive, 1 Block/Zyklus | sofortiger Restart true/false ohne Process, beide Formate | P0 | PL | gezielte Matrix statt bloß mehr Wiederholungen |
| Offline | TEILWEISE | 48 kHz/512, 1 stiller Block, bevorzugtes Format | längere Daten, Echtzeitvergleich, beide Formate | P0 | PL/PN indirekt | TESTER-015/016 |
| Latenz/Tail | TEILWEISE | Metadaten vor Setup | zulässiger Zeitpunkt, gemessene Verzögerung/Tail, Änderungen | keine spezifische | keine | TESTER-008 |
| Parameter-IDs/Metadaten | JA | Duplikate, Defaults, Istwerte, stepCount<0, Bypassanzahl | kaputte Metadaten-Fixtures, Unit-/String-Mapping | P0 exportiert keine Parameter | keine | TESTER-020 |
| Normalized/plain | TEILWEISE | 5 Punkte, endlich und Bereich | inverse Genauigkeit/quantisierte Schritte | keine | keine | konstante falsche Konversion kann passieren; TESTER-016 |
| Automation | TEILWEISE | bis 64 Params/Batch, Offsets 0/255/511, 1 Block/Batch, finite | tatsächliche Wirkung/Timing, Outputparameter, alle Formate | keine Param-Fixture | keine | TESTER-016/020 |
| Bypass | TEILWEISE | On/Off-Queues, finite Verarbeitung | Wirkung, Latenz, Tail, Übergang | keine Bypass-Fixture | keine | TESTER-016 |
| Component State Roundtrip | TEILWEISE | get/set-Rückgaben | nichtstandardmäßige Werte, semantische Wirkung | P0 nur leerer State | PS lehnt setState ab | TESTER-014 |
| Frische State-Instanz | TEILWEISE | Quelle zerstören, Restore und Re-save | ignorierter/teilweise verlorener State | P0 leer | PS; kein silent-ignore | TESTER-014 |
| Controller-/Private-State | TEILWEISE | setComponentState/private get/set | angezeigte Werte/GUI-Restore, Ressourcen | P0 nur Erfolgsrückgabe | keine | TESTER-014 |
| Preset-/Projekt-/Altstate | NEIN | kein Dateipreset-/DAW-Projekttest | Rückwärtskompatibilität, reale Projekte | keine | keine | produktspezifische Erweiterung |
| MIDI NoteOn/Off | TEILWEISE | 8 Paare in einem Block, Endlichkeit | NoteOn/Off-Wirkung, Mehrblockzuordnung | keine Event-Fixture | keine | TESTER-017 |
| Eventbusse/-kanäle | TEILWEISE | erster Input; i modulo Kanalzahl | weitere Busse; bei 16 Kanälen werden nur 0–7 gesendet | keine | keine | Reporting behauptet „across channels“ zu breit |
| Event-Offsets/Samplegenauigkeit | TEILWEISE | Inputwerte 0…511; nur Outputbereich prüfen | tatsächliche Reaktionszeit, Reihenfolge, Typ/Bus/Notenidentität | keine | keine | Nicht „Samplegenauigkeit bewiesen“ nennen |
| Event-Output/Overflow | TEILWEISE | bis 64 Outputevents, gültiger Offset | Überlaufzähler, zurückgewiesene NoteOffs | keine | keine | SDK-D; leere Ausgabe ist kein Funktionsnachweis |
| Transport/Tempo/PPQ/Pattern | NEIN | kein ProcessContext eingerichtet | playing/stop/seek/loop/tempo, stehende Noten | keine | keine | TESTER-017; nur anwendbare Plugins |
| GUI create/attach/detach | JA | 5 Zyklen, Frame, kurzer Messagepump, Fokus | paralleles Audio, viele Instanzen | PG | PE Crash zweites removed | TESTER-018 |
| GUI Resize/Scale/Minimize | TEILWEISE | Plugin kann resizeView anfragen | Resizevertrag, 100/150%, Minimieren/Reopen | PG feste Größe | keine | erst Handler/Hostgeometrie klären |
| GUI echte Bedienung | NEIN | kein Click-/Wheel-/Text-/Reset-Test | vollständige Bedienkette | keine | keine | produktspezifischer Vertrag und Studio One |
| Mehrere gleichzeitige Instanzen | NEIN | seriell, Source vor Destination zerstört | Interinstanzzustand/Isolation | keine | keine | keine Single-Instance-Plugins falsch verurteilen |
| Echtzeit-Deadline/CPU | TEILWEISE | Gesamtzeit mit Scan/Clear, Durchschnitt | paced Timing, p95/p99/max, Overruns, Messaufwand | keine Zeitkontrolle | keine | TESTER-019 |
| Callback-Allokation/Locks | NEIN | keine Instrumentierung | Alloc-/Block-/I/O-Nachweis | keine | keine | später gezielt, nicht aus Durchschnitt ableiten |
| Stufen-Crash/Hang/Exit | JA | Job, suspend/assign/resume, Timeouts | Child/Grandchild-Beendigung per Selbsttest | guard ok | guard fail/crash/hang | gute Basis, TESTER-012/020 |
| Bericht/Status/JSON | TEILWEISE | Zähler, Strings, Serialisierung | widersprüchliche/alte/defekte/fehlende Reports | P0 plus einfacher Ordnerscan | Defekt-Fixtures prüfen normale FAILs | TESTER-002/006/007 |
| Scanvollständigkeit/Zugriff | TEILWEISE | rekursiv, Zyklus-/Pfadidentität | unzugängliche Bereiche | zwei lesbare Fixtures | keine ACL-/Junction-Kontrolle | TESTER-003 |
| Unicode/Pfadvarianten | TEILWEISE | Wide-Starts, UTF-8-JSON; Bundle/Standalone | Codepages, CJK, lange/UNC-/Alias-Pfade | CI Bundle/Standalone, ASCII | keine | TESTER-010 |
| Parallele Läufe | NEIN | GUI sperrt eigenen Startbutton | prozessübergreifende Sitzungstrennung | keine | keine | TESTER-011 |
| Portable Extraktion/Paket | JA | zehn Ressourcen, Schreiben, Start, Cleanup | fehlende Ressource, Schreibausfall, Elterncrash | portable Good/Standalone | portable BadIO fachlich | keine Signatur-/Paketprovenienzprüfung |
| Validatorintegration | JA | Core, stdout/stderr-Log, -1/NT-Code-Mapping | Parservarianten/mehrere Klassen, Logausfall | Core-selftest im Workflow | kein synthetischer Wrapper-Code-Matrix-Test | Parser-selftest vorhanden, unaufgerufen |
| Standalone-Validator | NEIN | bewusst N/A bei regulärer .vst3-Datei | normale Validator-Audiotests für diese Form | standalone P0 ohne Validator | keine | Bericht muss fehlende Konformitätsebene zeigen |
| Studio One / Praxistest | NEIN | keine DAW-Automation | Laden/Bedienen/Projekt/Rescan/Editor unter realem Host | keine | keine | ausdrücklich NOT VERIFIED |

## 7. Bewertete Erweiterungen

Alle folgenden Einträge sind **Kategorie OPTIMIERUNG, Evidenz ENHANCEMENT, Status OPEN**. Ihre Existenz bedeutet keinen nachgewiesenen Pluginfehler. „Schweregrad“ bezeichnet hier die Bedeutung der Abdeckungslücke; es ist keine Reparaturfreigabe. Jede Erweiterung benötigt passende Positiv-/Negativkontrollen. Universelle VST3-Prüfung und produktspezifische Funktionsziele müssen getrennt bleiben.

### TESTER-014 — Semantische State-/Recall-Prüfung

**Kategorie:** OPTIMIERUNG; **Evidenz:** ENHANCEMENT; **Status:** OPEN.

**Schweregrad MEDIUM. Fundstellen:** [src/main.cpp:246–281](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/main.cpp#L246-L281), [src/state_probe.cpp:176–255](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/state_probe.cpp#L176-L255), [tests/qa_fixture.cpp:152–161](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/tests/qa_fixture.cpp#L152-L161), [tests/qa_fixture.cpp:195–204](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/tests/qa_fixture.cpp#L195-L204).

**Beobachtung:** Erfolg von get/set/re-save, aber leerer State reicht; Quelle wird nicht absichtlich von Default verschieden eingestellt. Ein setState, das Inhalte ignoriert und Erfolg meldet, kann diese Stufe bestehen.

**Vorschlag/Nutzen:** nichtstandardmäßige profilierte Parameter-/DSP-/MIDI-Zustände herstellen, speichern, ändern, auf frischer Instanz laden und beobachtbares Verhalten vergleichen. Private Controllerdaten separat behandeln. Hoch für Projektzuverlässigkeit; kein allgemeiner Bytegleichheitszwang.

**Risiko/Aufwand/Wartung:** MEDIUM; Zustandsmodelle und Zufall/IR/Ressourcen erfordern dokumentierte Erwartungen. **Regression:** korrekt wiederherstellendes und still ignorierendes/teilweise verlierendes Gegenfixture. **Offen:** welche 125A-Referenzzustände zuerst; Windows-Runtime.

### TESTER-015 — Echte Processing-Matrix und unabhängige Baselines

**Kategorie:** OPTIMIERUNG; **Evidenz:** ENHANCEMENT; **Status:** OPEN.

**Schweregrad MEDIUM. Fundstellen:** [src/main.cpp:284–348](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/main.cpp#L284-L348), [src/main.cpp:885–1004](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/main.cpp#L885-L1004).

**Beobachtung:** Matrix nur setup; echtes Audio meist 48 kHz/512. Automation/Bypass einmal insgesamt, Lifecycle/Offline bevorzugtes Format. Muster, Automation, Bypass und späteres Format laufen vor gemeinsamem State-Restore; Folgeprüfungen können von vorherigen Zustandsänderungen abhängen.

**Vorschlag/Nutzen:** repräsentative echte process-Matrix beider unterstützten Formate mit variierenden gültigen numSamples; Baseline je unabhängiger Kategorie; kombinierter Stresstest zusätzlich ausdrücklich markieren. Hoher Nutzen für block-/rateabhängige Defekte.

**Risiko/Aufwand/Wartung:** MEDIUM, mehr Laufzeit/Statekosten; kleine Pflichtmatrix plus optionales langes Profil. **Regression:** gutes variables Fixture und je ein rate-, block-, format- und reihenfolgeabhängig fehlerhaftes. **Offen:** Laufzeitbudget, notwendige Kombinationen.

### TESTER-016 — Funktionale Audio-, Parameter- und Bypassverträge

**Kategorie:** OPTIMIERUNG; **Evidenz:** ENHANCEMENT; **Status:** OPEN.

**Schweregrad MEDIUM. Fundstellen:** [src/main.cpp:202–230](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/main.cpp#L202-L230), [src/main.cpp:467–531](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/main.cpp#L467-L531), [src/main.cpp:584–731](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/main.cpp#L584-L731).

**Beobachtung:** Finite Outputsamples beweisen weder Signalweiterleitung noch wirksamen Bypass/Automation. Eine konstante falsche Normalized/plain-Abbildung kann in-range bleiben. GoodControl liefert immer Stille.

**Vorschlag/Nutzen:** einfache kontrollierbare Gain-/Delay-/Bypass-Fixture, unabhängige Zeit-/Wertorakel, Schritte/Quantisierung bei Konversion beachten; nichtstille AC-Signale und kanalseparate Signale. Für Fremdplugins Wirkung zunächst messen, nur gegen erklärte Verträge FAIL vergeben.

**Risiko/Aufwand/Wartung:** MEDIUM, Gefahr falscher Klang-/Bypassannahmen. Keine allgemeine „Ausgabe muss Eingang entsprechen“-Regel für Synths, Ducker oder Hall. **Regression:** ignorierte Automation, falscher Offset, defekter Bypass und falsche Konversion gegenüber korrekten Kontrollfällen. **Offen:** produktbezogene Sollwerte/Toleranzen.

### TESTER-017 — Mehrblockige Events und optionaler Transportkontext

**Kategorie:** OPTIMIERUNG; **Evidenz:** ENHANCEMENT; **Status:** OPEN.

**Schweregrad MEDIUM. Fundstellen:** [src/io_event_probe.cpp:484–593](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/io_event_probe.cpp#L484-L593); SDK-D.

**Beobachtung:** ein Eventblock, erster Eingang, maximal acht adressierte Kanäle; PPQ stets 0, kein ProcessContext; Output nur Offsetbereich, EventList-Kapazität 64 ohne eigene Ablehnungsstatistik.

**Vorschlag/Nutzen:** alle angekündigten relevanten Busse/Kanäle, gehaltene Noten über Blöcke, Note-ID/Off-Zuordnung, validen Tempo-/Playing-/Seek-/Loop-Kontext, bekannte Ausgabeverträge und kontrollierte Host-Backpressure prüfen. Hoch für Rifferator/MIDI-Tools.

**Risiko/Aufwand/Wartung:** MEDIUM–HIGH; MIDI-Funktionen und musikalische Erwartungen sind optional/profilabhängig. **Regression:** guter Echo-/Generatorfall, falscher Offset/Bus, verlorene NoteOff, Host lehnt definierte Events ab; Folgeblock darf keine Duplikate/hängenden Noten erzeugen. **Offen:** jeweiliger Produktvertrag.

### TESTER-018 — Editor unter Audio/Automation und reale Bedienung

**Kategorie:** OPTIMIERUNG; **Evidenz:** ENHANCEMENT; **Status:** OPEN.

**Schweregrad MEDIUM. Fundstelle:** [src/editor_lifecycle_probe.cpp:279–425](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/editor_lifecycle_probe.cpp#L279-L425).

**Beobachtung:** fünf kurze serielle Zyklen ohne Audio, keine Bedien-/Skalierungs-/Mehrinstanzabdeckung. Ein nacktes IPlugView-Fixture beweist keine VSTGUI-Interaktion.

**Vorschlag/Nutzen:** nach TESTER-005 validen UI-Thread/Audio-Thread aufbauen; Editor mehrfach während Audio/Automation öffnen, schließen, minimieren und unterstützte Größenwechsel prüfen; definierte Klick-/Wheel-/Reset-/Textaktionen anhand Produktprofil.

**Risiko/Aufwand/Wartung:** HIGH, Hostgeometrie/DPI/Timing können False Positives erzeugen. Keine Behauptung vollständiger GUI-QA ohne realen Zielhost. **Regression:** PG/PE erhalten plus guter aktiver Editor und nur bei Reopen/Audio versagendes Gegenfixture. **Offen:** Windows-Desktop und Studio-One-Matrix.

### TESTER-019 — Aussagefähige Echtzeit- und Audiokontinuitätsmessung

**Kategorie:** OPTIMIERUNG; **Evidenz:** ENHANCEMENT; **Status:** OPEN.

**Schweregrad MEDIUM. Fundstelle:** [src/main.cpp:534–580](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/main.cpp#L534-L580).

**Beobachtung:** Gesamtzeit inklusive Clear/Descriptorarbeit/Outputscan, stiller Input, keine zeitliche Taktung, nur arithmetischer Durchschnitt; kein Deadline-/Alloc-/Locknachweis.

**Vorschlag/Nutzen:** process-Zeit getrennt und mit gemessenem Timeraufwand erfassen, p95/p99/max/Overruns, Rate/Block/CPU/OS/Last dokumentieren; Offline-Durchsatz von getaktetem Echtzeittest trennen und nichtstille Kontinuität prüfen.

**Risiko/Aufwand/Wartung:** HIGH bei harten universellen CI-Grenzen; zuerst Diagnosemodus und gleiche-Session-Baseline. Danach gezielte Callback-Allokationsprüfung statt invasive pauschale Instrumentierung. **Regression:** kontrolliert langsamer und zeitweise blockierender Fall plus messbar ausreichender positiver Fall. **Offen:** Host-/Hardwarebudget und verbindliche Grenzwerte.

### TESTER-020 — Tester-Selbstvalidierung gezielt vervollständigen

**Kategorie:** OPTIMIERUNG; **Evidenz:** ENHANCEMENT; **Status:** OPEN.

**Schweregrad MEDIUM. Fundstellen:** [tests/qa_fixture.cpp:142–267](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/tests/qa_fixture.cpp#L142-L267), [src/guard_selftest.cpp:23–46](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/guard_selftest.cpp#L23-L46), [.github/workflows/build-windows.yml:22–151](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/.github/workflows/build-windows.yml#L22-L151).

**Beobachtung:** sieben Varianten eines einfachen Stereo-Fixtures; keine Parameter-/MIDI-/Aux-Fixture. Zusätzlich überschreibt keine Variante canProcessSampleSize: die SDK-Basis AudioEffect meldet nur kSample32. Die vorhandenen Sample64-Zweige im Fixture-Prozesscode sind dadurch über den normalen Testerpfad nicht erreichbar; diese Fixtures sichern den 64-Bit-Pfad nicht ab. Leere State-Erfolgsantworten sind keine semantische Positivkontrolle. Guard testet keine Nachfahren. Releasecode verwendet konkrete Rückgabevergleiche, keine deaktivierbaren assert als Hauptgates.

**Vorschlag/Nutzen:** zuerst kleine korrekte Gegenfälle für TESTER-001/005 und Status-/Reportfehler sowie State-ignore. Dann explizite Sample64-Unterstützung beziehungsweise 64-Bit-only-Gegenfälle sowie Parameter/Aux/Events, jeweils genau eine absichtliche Vertragsverletzung. Bestehende Kontrollen unverändert als Baseline erhalten.

**Risiko/Aufwand/Wartung:** LOW–MEDIUM; bekannte, unabhängige Sollwerte statt Tests, die nur Implementierung spiegeln. **Regression:** jede Negativvariante muss gezielt erkannt werden, alle legitimen Ablehnungen müssen bestehen. **Offen:** Ausführung vorhandener CI zuerst nachweisen, bevor weitere bezahlte Läufe genehmigt werden.

### TESTER-021 — Diagnose, Provenienz und Wartbarkeit ohne Großumbau

**Kategorie:** OPTIMIERUNG; **Evidenz:** ENHANCEMENT; **Status:** OPEN.

**Schweregrad LOW. Fundstellen:** [src/launcher_diagnostics.cpp:37–82](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/launcher_diagnostics.cpp#L37-L82), [src/launcher_diagnostics.cpp:169–192](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/launcher_diagnostics.cpp#L169-L192), [src/io_event_probe_logged.cpp:83–99](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/io_event_probe_logged.cpp#L83-L99), [src/dispatcher.cpp:322–365](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/src/dispatcher.cpp#L322-L365), [CMakeLists.txt:58–80](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321/CMakeLists.txt#L58-L80).

**Beobachtung:** Parser-Selbsttest existiert ohne Workflowaufruf; Validatorparser dedupliziert identische Test-/Fehlertexte ohne Klassenkennung. I/O-Log wird erst nach Probeende aus unbeschränktem RAM-Capture geschrieben; beim Crash fehlen letzte Phasen. JSON enthält Findings als Strings, keine strukturierte Pflichtstufenliste oder Build-/Binaryhashes. Mehrere cpp-Includes/alternative Release-Dateien erschweren die Bestimmung aktiver Pfade.

**Vorschlag/Nutzen:** zunächst aktive Targets, Version/Commit/Run-ID und geprüfte Stufen dokumentieren, Parsertests regulär nutzen; begrenzte stagebezogene Crashdiagnostik und Klassenkennung. Keine umfassende Architekturumschreibung.

**Risiko/Aufwand/Wartung:** LOW für Dokumentation/Metadaten, MEDIUM für neue Diagnostik; Logging nicht in gemessenen Audiocallback verlagern. **Regression:** mehrere Klassen mit gleichem Validatorfehler, Crash mitten in Probe, große Ausgabe, Parserformatänderung. **Offen:** welche Reportfelder verpflichtend werden; rückwärtskompatibles Schema.

## 8. Priorisierte Entscheidungsvorlage

| Gruppe | Reihenfolge / Maßnahme | Technische Begründung / Aufwand |
| --- | --- | --- |
| Notwendige Korrekturen zur Vertrauenswürdigkeit | 1: TESTER-001 und -005 mit positiven Hostkontrollen | Eigene ungültige Hostdaten dürfen keinen Pluginfehler vortäuschen. Mittel bis hoch. |
| Notwendige Korrekturen zur Vertrauenswürdigkeit | 2: TESTER-002, -006, -007 gemeinsam mit Status-/I/O-Fehlerinjektion | Ein unvollständiger oder abgebrochener Lauf darf nicht PASS werden. Mittel. |
| Notwendige Korrekturen | 3: TESTER-003, -008, -009 | Vollständigkeit, gültige Lebenszyklusphase und ehrliche Stufenergebnisse. Niedrig bis mittel. |
| Dokumentationskorrektur | TESTER-004 bei freigegebenem separatem Umfang | Versionen/Betriebsbeschreibung an tatsächlichen Stand angleichen, keine Versionsanhebung. Niedrig. |
| Sinnvolle zusätzliche Absicherung | TESTER-020, danach -014/-015 | Erst Tester-Orakel sichern, dann Recall und echte Processingkombinationen. Mittel. |
| Verdachtsklärung | TESTER-010/-011/-012/-013 | Reproduzieren beziehungsweise Anforderungen klären; keine spekulative Reparatur. |
| Optionale profilspezifische Erweiterungen | TESTER-016/-017/-018 | Hoher Nutzen, aber Produktvertrag/Windows-/DAW-Orakel erforderlich. Mittel bis hoch. |
| Optionale Diagnose | TESTER-019/-021 | Timing/Provenienz verbessern; keine vorschnellen Performance- oder Konformitätsgates. |

**Vorerst abzuraten:** große Refactorings der Include-Architektur, globale Verschärfung aller WARNs zu FAIL, pauschale Klang-/THD-/DC-/Lautheitsgrenzen für fremde Plugins, Bytegleichheit beliebiger States, unbegrenzte Vollkombinationsmatrizen als Standard, reine Erhöhung der Wiederholungszahl, automatische Referenzneuerzeugung, invasive Callback-Hooks ohne Messaufwandsnachweis und Windows-/Studio-One-Freigaben aus Linux- oder Validatorergebnissen.

Es wird kein Linux-/macOS-Port empfohlen. Der frühere Linux-Buildfehler ist für den gewählten Windows-Arbeitsablauf kein zu reparierender Produktbefund.

## 9. Ausstehende Nachweise und zukünftiges Prüfprotokoll

1. Zugriff auf GitHub API/Actions/Artefakte herstellen; aktuelle Run-ID, head_sha, conclusion und tatsächliche Schrittlogs lesen. Proxy-403 nicht durch neue Tokens oder eine vermutete Buildwiederholung „lösen“.
2. Konkretes Nutzerpaket mit SHA-256 und öffentlicher EXE-Hash beschaffen; Source-/Build-/Paketkette schließen.
3. Auf Windows zunächst vorhandene Prüfungen nachvollziehen, ohne neuen Workflow oder Buildprozess zu ändern. Neue CI-Läufe benötigen Freigabe gemäß Auftrag.
4. Für TESTER-001…009 gezielte Positiv-/Negativ-Reproduktionen mit reproduzierbarer Umgebung und exakten Logs. Statische Bestätigung und tatsächlicher Laufnachweis getrennt erfassen.
5. Verdachtsfälle TESTER-010…013 mit Codepage-, Parallelitäts-, Prozessbaum- und Mix-FX-spezifischen Fällen klären.
6. Für eventuelle freigegebene Reparatur: separater Entwicklungsbranch, unveränderte letzte funktionsfähige EXE/Quelle als Baseline, vorher/nachher, passende Negativ- und Positivkontrollen, keine Abschwächung vorhandener Gates.
7. Studio One separat: Scan/Laden, bestehendes Projekt, mehrere zulässige Instanzen, supported Rate/Block, Automation/Bypass, GUI öffnen/schließen/reopen unter Audio, Projekt speichern/reload und längere reale Nutzung dokumentieren. Ein erfolgreicher automatisierter Tester ersetzt das nicht.

Bis dahin bleibt die tatsächlich belegte Zuverlässigkeit auf statische Eigenschaften begrenzt. Der vorhandene Tester kann nützliche Defekte finden; weder vollständige Sensitivität noch Spezifität sind nachgewiesen. Ein positives Ergebnis bestätigt nur ausgeführte und gültige Prüfungen, keine generelle DAW-Stabilität.

## 10. Änderungshistorie, Erhaltung alter Befunde und Übergabe

| Datum | Befund / Änderung | Begründung |
| --- | --- | --- |
| 2026-10-15 | TESTER-001…004 aus früherer Teilprüfung unabhängig erneut am identischen Quellstand geprüft | Codepfade und SDK-Verträge bestätigen die statischen Befunde; kein Upgrade zu Runtime Confirmed |
| 2026-10-15 | Früherer Unicode-Verdacht als TESTER-010 erhalten | Laufzeit-Codepage unbekannt; bleibt SUSPECTED |
| 2026-10-15 | TESTER-005…009 ergänzt | Zusätzliche Host-, GUI-, Report- und Probe-Fehlerpfade |
| 2026-10-15 | TESTER-011…013 und TESTER-014…021 ergänzt | Konkrete Verdachtsklärung und abgegrenzte Abdeckungsverbesserungen |
| 2026-10-15 | Vollständigkeit der früheren Teilprüfung ausdrücklich korrigiert | Kein vollständiges Runtime-/Praxisaudit; fehlende CI-/Paketnachweise sichtbar |
| 2026-10-15 | Neuer Bericht auf separatem Dokumentationsbranch | Autorisierte reine Dokumentation; keine technische Änderung |

**Statusänderungsregel:** IDs bleiben stabil. Keine Befunde löschen oder kommentarlos umklassifizieren. Jede Änderung braucht Datum, alte/neue Kategorie bzw. Status, technischen Grund, Quellcommit und überprüfbaren Nachweis. APPROVED ist eine Benutzerentscheidung; FIXED benötigt gesonderte Codefreigabe, Commit und Regression. REJECTED benötigt Gegenbeleg.

**Übergabe:** 9 CODE CONFIRMED / 0 RUNTIME CONFIRMED / 4 SUSPECTED / 8 ENHANCEMENT. Keine Reparatur umgesetzt. Der Titel des gewünschten Dokumentations-PRs lautet „AUDIT: Complete technical review of 125A Plugin Tester v0.2.9“; dieser Titel ersetzt nicht die hier ausdrücklich dokumentierten offenen Nachweise. GitHub-Berichtslink/PR-Link werden erst als vorhanden bezeichnet, wenn ihre Erstellung beziehungsweise der Branch-Push bestätigt ist.

## 11. Historische Bereitstellung und damaliger Zugangsblocker

**Historischer Stand vor PR #3:** Dieser Abschnitt bleibt zur Nachvollziehbarkeit erhalten. Inzwischen ist der Bericht über PR #3 auf main gelandet, per GitHub-Webseite und Raw-Abruf bestätigt. Für die Fortsetzung gilt die aktuelle Benutzerfreigabe für einen reinen Dokumentations-PR samt Merge. Der aktuelle Zugangsstatus steht in Abschnitt 12.

Der Dokumentationsbranch wurde am 2026-10-15 erfolgreich über den vorhandenen GitHub-HTTPS-Git-Zugang gepusht und anschließend per git ls-remote verifiziert. main blieb unverändert auf dem untersuchten Quellcommit. Der Vergleich zur Prüfbasis enthält ausschließlich die neue AUDIT-REPORT.md; alle vorhandenen Dateien sind bytegleich.

- [Bericht auf dem Dokumentationsbranch](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/docs/audit-plugin-tester-v0.2.9-2026-10-15/AUDIT-REPORT.md)
- [Dokumentationsdiff gegen main](https://github.com/challanger2000/125A-Plugin-Tester-Final/compare/main...docs/audit-plugin-tester-v0.2.9-2026-10-15)
- [GitHub-Formular zum Anlegen des angeforderten PRs](https://github.com/challanger2000/125A-Plugin-Tester-Final/pull/new/docs/audit-plugin-tester-v0.2.9-2026-10-15) — **kein bereits erstellter PR**.

Der ausdrückliche Versuch mit gh pr create, Basis main, diesem Dokumentationsbranch, dem angeforderten Titel und einer vollständigen Dokumentationsbeschreibung scheiterte mit: Post https://api.github.com/graphql: Forbidden. Zuvor scheiterten Actions-/Release-Leseabfragen; curl zeigte bereits beim CONNECT-Tunnel einen Proxy-403. API-Zugang ist damit blockiert; daraus folgt weder ein fehlender GitHub-Token noch das Fehlen erfolgreicher CI-Läufe. Keine Tokenwerte wurden angefordert, ausgelesen oder dokumentiert.

**Noch erforderlich:** erlaubter und autorisierter HTTPS-Zugang zu api.github.com für CI-/Artefaktlesen und PR-Erstellung. Danach die betroffenen Abfragen und PR-Erstellung erneut ausführen, tatsächlichen PR-Link ergänzen; niemals einen PR als erstellt behaupten, nur weil ein Formularlink existiert. Alternativ kann der Nutzer den Dokumentations-PR über das verlinkte GitHub-Formular anlegen. Kein Merge durchführen. Die Dokumentation ist bereits dauerhaft auf dem Remote-Branch vorhanden, die PR-Bereitstellung bleibt offen.

## 12. Fortsetzung: zusätzliche Nachweise am aktuellen main

### 12.1 Basis, Autorisierung und Grenzen

Die Fortsetzung setzt auf main **12f92a702c4369de1e6d1b2075178a6a47cf4f2d** auf. [PR #3](https://github.com/challanger2000/125A-Plugin-Tester-Final/pull/3) ist auf der regulären GitHub-Webseite als Merged erkennbar; der Commit und der vollständige Bericht wurden über Git geprüft. Zusätzlich lieferte der Raw-Abruf von main HTTP 200. Der SHA-256 des übernommenen unveränderten Erstberichts lautet f5459f87456291079ac7e8687178dea7ea0469374e2fd4494ad5fafd85d3b6de. Damit ist die frühere Bereitstellung auf main nun nachgewiesen.

Der Diff von 513cf9a zu diesem main enthält ausschließlich AUDIT-REPORT.md. Alle 30 zuvor vorhandenen Nicht-Audit-Dateien sind bytegleich. Die technischen Quellverweise der Erstprüfung bleiben deshalb gültig; die Untersuchung wurde nicht von vorn begonnen. Engineering START-HERE wurde zuerst erneut gelesen; Engineering-main ist weiterhin 304ebeb6c965949b3e389adf8109f9bcb333e8f8.

Aktueller Arbeitsbranch: **docs/audit-evidence-continuation-2026-10-15**. Nur Audit-Dokumentation darf geändert werden. Ein ausschließlich dokumentierender PR darf laut Folgeauftrag selbst zusammengeführt werden; Code, Tests, Builddateien und Versionen bleiben unberührt. Neue CI-Läufe sind nicht freigegeben. Weil der vorhandene Workflow auf push nach main reagiert, soll ein etwaiger Dokumentations-Squash-Merge die unterstützte Kennzeichnung [skip ci] im Committext erhalten, statt den Workflow zu verändern.

### 12.2 Zugangsprüfung: API blockiert, reguläre Webseiten nutzbar

Die Versuche mit gh api für actions/runs und releases scheitern weiterhin mit Forbidden; HTTPS zu api.github.com wird bereits beim CONNECT-Tunnel mit Proxy-403 abgewiesen. Die gelesene veröffentlichte Umgebungskonfiguration enthält inzwischen bereits die Regel api.github.com. Eine weitere identische Domain-Ergänzung wäre deshalb keine neue Diagnose und wurde nicht angefordert. Die tatsächliche Laufzeitrouting-/Proxy-Freigabe ist trotz vorhandener Konfiguration nicht belegt. GH_TOKEN ist als Bindung vorhanden; sein Wert wurde weder angezeigt noch ausgelesen oder kopiert. Aus dem Proxyfehler folgt keine Aussage über die Gültigkeit dieses Tokens.

Reguläre, rein lesende Abrufe von github.com und raw.githubusercontent.com funktionieren. So wurden vorhandene Actions-Seiten, Jobseiten, Artefaktmetadaten, die leere Releases-Seite, PR #3 und der Bericht auf main abgerufen. Das ist ein zusätzlicher zulässiger Nachweisweg; es wurden keine Authentifizierungssperren umgangen. Detail-Logs und Artefaktdownloads blieben hinter dem dortigen Zugriffsschutz: „Sign in to view logs“, Logabruf HTTP 404 und Artefaktabruf nach Weiterleitung auf die GitHub-suites-Seite HTTP 404. Ein fehlgeschlagener geschützter Abruf ist kein Beweis gelöschter Logs oder Artefakte.

### 12.3 Tatsächlich geprüfte vorhandene Windows-CI-Läufe

Aus der Actions-Übersicht wurden vollständige Quell-SHAs und Runzustände gelesen und mit den jeweiligen Run-/Jobseiten abgeglichen. Alle folgenden Läufe sind **COMPLETED / SUCCESS**. Es wurden keine neuen Läufe ausgelöst.

| Run | Quellcommit | Erstellt (UTC; Europe/Berlin +02:00) | Job | Einzel-Schrittstatus |
| --- | --- | --- | --- | --- |
| [#6 / 38715638919](https://github.com/challanger2000/125A-Plugin-Tester-Final/actions/runs/38715638919) | 12f92a702c4369de1e6d1b2075178a6a47cf4f2d | 2026-10-15T09:48:20Z | [116357522714](https://github.com/challanger2000/125A-Plugin-Tester-Final/actions/runs/38715638919/job/116357522714) | 14 success, 1 skipped |
| [#5 / 38649898071](https://github.com/challanger2000/125A-Plugin-Tester-Final/actions/runs/38649898071) | 513cf9a126d5a70ac2bb3c3da3990e9b3c8ab321 | 2026-10-14T22:46:26Z | [116145813231](https://github.com/challanger2000/125A-Plugin-Tester-Final/actions/runs/38649898071/job/116145813231) | 14 success, 1 skipped |
| [#4 / 38645329416](https://github.com/challanger2000/125A-Plugin-Tester-Final/actions/runs/38645329416) | 6ddec5127aec2c207becdf841c5e56c7b5fb8128 | 2026-10-14T22:06:54Z | [116130639821](https://github.com/challanger2000/125A-Plugin-Tester-Final/actions/runs/38645329416/job/116130639821) | 14 success, 1 skipped |
| [#3 / 37432640438](https://github.com/challanger2000/125A-Plugin-Tester-Final/actions/runs/37432640438) | fd246146994384e301c735e7d860b53aa06b18c5 | 2026-10-06T07:54:46Z | [112166862097](https://github.com/challanger2000/125A-Plugin-Tester-Final/actions/runs/37432640438/job/112166862097) | 14 success, 1 skipped |

Bei allen vier Jobs wurden die folgenden **tatsächlich gemeldeten Schrittzustände** gelesen; sie stimmen mit dem Workflow am zugehörigen Commit überein:

| Schritt | Name | Zustand in allen vier Jobs |
| --- | --- | --- |
| 1 | Set up job | success |
| 2 | Checkout | success |
| 3 | Configure | success |
| 4 | Build | success |
| 5 | Verify production Job Object guard | success |
| 6 | Verify Steinberg validator self-test | success |
| 7 | Verify tester specificity, sensitivity and JSON with VST3 fixtures | success |
| 8 | Verify serial folder scan and aggregate JSON | success |
| 9 | Stage single-file portable package | success |
| 10 | Verify final portable EXE end to end | success |
| 11 | Ensure only one public executable is shipped | success |
| 12 | Upload fixture diagnostics on failure | skipped |
| 13 | Upload package | success |
| 26 | Post Checkout | success |
| 27 | Complete job | success |

**Was sich damit verbessert:** Windows-Build und die benannten vorhandenen automatischen QA-Gates sind auf Ebene der von GitHub gespeicherten Lauf-/Schrittmetadaten erfolgreich nachgewiesen. Der zuvor nur als Kandidat bekannte Quellcommit fd24614 besitzt einen erfolgreichen Windows-CI-Lauf; auch der genaue Erstprüfcommit 513cf9a und der aktuelle main 12f92a7 haben erfolgreiche Läufe. Die Erfolgsaussage ist somit stärker als bloß gelesener Workflowtext.

**Was weiterhin fehlt:** Die Konsolenausgaben und einzelnen Testergebnisse waren nicht zugänglich. Deshalb werden weder konkrete Validator-Testzahlen noch einzelne Fixture-PASS-/FAIL-Zahlen als aus Logs verifiziert ausgegeben. Dass ein Schritt als success gespeichert ist, ersetzt keine nachträgliche Prüfung seiner Assertions, Testvollständigkeit oder eines Nutzerpakets. Der erwartungsgemäß übersprungene Fehlerdiagnostik-Upload ist kein übersprungener fachlicher Test. Windows-Laufzeitbelege gelten hier für die vorhandenen kontrollierten CI-Gates, nicht für unsere neu benannten Fehler-/Verdachtsfälle, beliebige Fremdplugins oder Studio One.

Die früheren Abschnitte 2 und 4 sind als Erststand zu lesen: „keine zugänglichen CI-Nachweise“ ist durch diese Fortsetzung teilweise überholt; fehlende Binär-/Log- und Nutzerpaketnachweise bleiben bestehen. Bereits der Merge von PR #3 hat laut Run #6 einen bestehenden push-main-Workflow ausgelöst; dieser Lauf existierte vor dieser Fortsetzung und wurde hier nur gelesen.

### 12.4 Artefakte, Prüfsummen und Releasezuordnung

Jede der vier Runseiten zeigt ein Artefakt namens **125A-Plugin-Tester-Windows-x64**, Anzeigegröße **840 KB**:

| Run | Artefakt-ID | Von GitHub angezeigter Artefakt-Digest |
| --- | --- | --- |
| 38715638919 | 11962902194 | sha256:81f1805269137daae917ea97322ff9cc06679103ae53ed8b45256016b4917061 |
| 38649898071 | 11934626728 | sha256:52355758026ab3e6edacd28199ab9f2beb4332e49a7c2e4dc62ce5175fd35864 |
| 38645329416 | 11931972211 | sha256:7485eb04094e4f4fa354d7ca0a3a25a6cfc54cb8002d69f671e05b43312453b1 |
| 37432640438 | 11397432393 | sha256:3c0b8e2f9e29fe31ec02dbac08f14e3c3af4453b9fbebf808588d22774cc30a1 |

Dies sind **abgelesene Metadaten**, keine lokal unabhängig überprüften Downloads. Der Digest darf nicht als SHA-256 der enthaltenen öffentlichen EXE oder des Nutzer-ZIPs 125A-Plugin-Tester-v0.2.9-Windows-x64 ausgegeben werden. Das Artefakt konnte nicht heruntergeladen werden; Inhalt, PE-Metadaten, eingebettete Helfer und Binärhashes sind deshalb weiterhin NOT VERIFIED. Unterschiedliche Digests bei gleicher technischer Quelle beweisen keine Fehlfunktion; deterministische Builds oder identische Paketbytes wurden nicht zugesichert.

Die reguläre Releases-Seite antwortete HTTP 200 und zeigte „There aren’t any releases here“ ohne Release-Downloadlinks. Damit sind auf dieser zugänglichen Seite keine veröffentlichten GitHub-Releases ausgewiesen. Das schließt Actions-Artefakte oder ein externes Nutzerpaket ausdrücklich nicht aus.

Die lokale Bestandsaufnahme fand keine Windows-EXE, kein ZIP und kein VST3-Binärartefakt des Testers. Lediglich eine mit dem SDK vorhandene drumLoop.wav wurde gefunden; sie wurde nicht als 125A-Referenzmaterial interpretiert oder verwendet. Eine vorhandene Quelldatei ist kein heruntergeladenes CI-Paket.

### 12.5 Lokale ausgeführte Logikprüfung — 24 Beobachtungen

**Ebene:** native Linux-C++-Prüfung unveränderter ausgewählter Quelltextabschnitte, kein Build oder Lauf der Windows-Anwendung. Compiler GCC 14.2.0, C++17, -O0. Es wurden ausschließlich temporäre Harness-/Ergebnisdateien unter /tmp/125a-audit-evidence erzeugt; keine Produktionsquelle, Repository-Testdatei oder Buildkonfiguration geändert.

Die Funktionen wurden aus dem bytegleichen aktuellen Quellstand anhand fester Beginn-/Endmarkierungen extrahiert, nicht neu implementiert. Ein minimales Metadatenobjekt liefert BusInfo für den Buskonstruktor; die AudioBusBuffers- und Sampletypen stammen aus dem tatsächlich gepinnten SDK. Für die Klassifizierung liefert ein einfacher ReportPaths-Adapter ausschließlich Testdateinamen im Tempverzeichnis. GUI-Zeilen werden in einem Vektor gesammelt, nicht in Win32-Fenstern angezeigt. Weder echte Plugins noch Process-Aufrufe, Win32-Job Objects, CRT-Unicodeargumente oder GUI-Threads werden damit getestet.

**Ergebnis:** 24 von 24 erwarteten Beobachtungen reproduziert; 0 Abweichungen von den vorab festgelegten Beobachtungen. Das bedeutet ausdrücklich **nicht**, dass die Software 24 korrekte Produktprüfungen bestanden hat: mehrere Erwartungen beschreiben die nachgewiesenen Fehler. Positive Kontrollen sichern ab, dass nicht einfach jede Eingabe fehlschlägt oder PASS liefert.

| Bezug | Ausgeführte Beobachtung | Ergebnis und Reichweite |
| --- | --- | --- |
| TESTER-001 | inaktiver Stereo-Bus float und double | zweimal numChannels=2 bei null Kanalzeiger-Array; Hostdatenfehler lokal reproduziert |
| TESTER-001 | aktiver Stereo-Bus float und double | zweimal zwei angelegte Samplepuffer; positive Kontrollen |
| TESTER-002 | bloße RESULT-PASS-Zeile mit Exit 0, 1, 2, 124, 125, 126, 127 | siebenmal PASS; Fehler-Exitcodes werden tatsächlich überstimmt |
| TESTER-002 | negative PASS-/WARNING-Zähler bei FAIL=0, Exit 126 | PASS; Plausibilitätslücke reproduziert |
| TESTER-002 | gültiger FAIL-Bericht, gültiger WARNING-Bericht, kein Bericht, Guard plus QA | vier Gegenkontrollen liefern FAIL, WARNING, INCONCLUSIVE, INCONCLUSIVE |
| TESTER-006 | Ordnersummary mit Discovery Error und Exit 1 | GUI-Statusfunktion liefert PASS |
| TESTER-006 | fehlender Einzelbericht mit Exit 127 | obere Statusfunktion liefert INCONCLUSIVE, Kurztextfunktion daneben PASS; zwei Beobachtungen |
| TESTER-006 | expliziter FAIL-Befund | Kurztextfunktion liefert FAIL; Gegenkontrolle |
| TESTER-007 | normal beschreibbarer Bericht | true; positive Kontrolle |
| TESTER-007 | unabhängiger Schreib-/Flushversuch auf /dev/full | Fehlerzustand; Betriebssystem-Fehlerquelle geprüft |
| TESTER-007 | große Berichtsausgabe auf /dev/full | Reporter::write liefert dennoch true; Fehlerweitergabelücke lokal reproduziert |
| TESTER-021 | vorhandener runValidatorParserSelfTest | Rückgabewert 0; drei enthaltene synthetische Parserfälle akzeptiert, kein offizieller Steinberg-Validatorlauf |

Gesamter ungekürzter Laufoutput:

~~~text
OBSERVED 001 inactive float bus: numChannels=2 and array=null
OBSERVED 001 inactive double bus: numChannels=2 and array=null
OBSERVED 001 active float control: two allocated buffers
OBSERVED 001 active double control: two allocated buffers
OBSERVED 002 bare PASS summary overrides exit 0
OBSERVED 002 bare PASS summary overrides exit 1
OBSERVED 002 bare PASS summary overrides exit 2
OBSERVED 002 bare PASS summary overrides exit 124
OBSERVED 002 bare PASS summary overrides exit 125
OBSERVED 002 bare PASS summary overrides exit 126
OBSERVED 002 bare PASS summary overrides exit 127
OBSERVED 002 negative counters accepted as PASS
OBSERVED 002 FAIL control
OBSERVED 002 WARNING control
OBSERVED 002 missing report control is INCONCLUSIVE
OBSERVED 002 guard control overrides QA
OBSERVED 006 discovery error plus exit1 gives GUI PASS
OBSERVED 006 missing plugin report plus exit127 top status is INCONCLUSIVE
OBSERVED 006 same missing report short explanation is PASS
OBSERVED 006 FAIL explanation control
OBSERVED 007 normal writable report control
OBSERVED 007 independent /dev/full write fails
OBSERVED 007 Reporter::write nevertheless returns true on /dev/full
OBSERVED 021 existing validator parser selftest
TOTAL=24 OBSERVATIONS_MATCHED=24 MISMATCHES=0
~~~

SHA-256 des erzeugten C++-Harness: b464f888e9bfe04f736c6d11b4ddc75eb84dcc5e935758203c66155886b909d1. SHA-256 der Ergebnisdatei: ada0db125b21573f91273ef2561d7957b1f9fcbd85db9a6e90bfcc7b89fb594c. SHA-256 des unten dokumentierten Extraktions-/Testskripts: 2cd635c5cdb9cba6e6e44edb281067b03bfe68a337c61d127c323592f30bcf05. Die einzelnen extrahierten Funktionen und deren Hashes werden im Skript erzeugt. Hashes identifizieren dieses lokale Experiment, nicht die öffentliche Windows-EXE.

### 12.6 Erneute Bewertung aller bisherigen IDs

Keine IDs gelöscht, keine Reparatur behauptet, kein Verdacht ohne geeignete Reproduktion zu einem Windows-Fehler hochgestuft. Alle Befundstatus bleiben OPEN. Die Anzahl bleibt **9 bestätigte Fehler, 4 Verdachtsfälle, 8 Verbesserungen**. Von den neun Codebefunden besitzen vier jetzt zusätzliche lokale Logikreproduktionen; es gibt weiterhin **0 neu ausgeführte Windows-E2E-Reproduktionen dieser Befunde**.

| ID | Neuer Prüfstand / zusätzliche Evidenz | Nachweisklasse und offene Grenze |
| --- | --- | --- |
| TESTER-001 | exakt extrahierter Worker-Buskonstruktor erzeugt in beiden Sampletypen das beanstandete Array; aktive Gegenkontrollen korrekt | CODE CONFIRMED + lokal reproduzierte Konstruktorlogik; tatsächliche Pluginreaktion offen |
| TESTER-002 | sieben Exitvarianten, negative Zähler und vier Gegenkontrollen ausgeführt | CODE CONFIRMED + lokal reproduzierte Klassifizierungslogik; Windows-Prozesskette offen |
| TESTER-003 | unveränderter Skip-/Permission-Fehlerpfad erneut geprüft; CI-Ordnerscan hat nur lesbare einfache Fixtures | CODE CONFIRMED; keine Windows-ACL-Reproduktion |
| TESTER-004 | Ressourcen 0.2.0, Launcher/Guard 0.2.5 und Projekt 0.2.9 erneut abgeglichen | CODE CONFIRMED; tatsächliche PE-Metadaten des Artefakts/Nutzerpakets offen |
| TESTER-005 | weiterhin kein setComponentHandler-Aufruf; aktuelle Controllerpfade und gepinnter SDK-Vertrag unverändert | CODE CONFIRMED; kein strenges Controller-/Editorfixture ausgeführt |
| TESTER-006 | Discovery-Fehler-PASS und widersprüchliche Status-/Kurztexte lokal ausgeführt, FAIL-Gegenkontrolle vorhanden | CODE CONFIRMED + lokal reproduzierte Text-/Statuslogik; keine Win32-Fensterprüfung |
| TESTER-007 | Reporter-Methode mit realem Linux-I/O-Fehler und normaler Datei ausgeführt | CODE CONFIRMED + lokaler I/O-Logiknachweis; Windows-Sperr-/Volume-/Annotationspfade offen |
| TESTER-008 | Latenz/Tail stehen weiterhin vor erstem Processing-Setup | CODE CONFIRMED; keine rateabhängige Runtimekontrolle |
| TESTER-009 | unveränderte Controller-Creation-/N/A-Pfade erneut abgeglichen; vorhandene sieben Fixtures decken diese Fälle nicht ab | CODE CONFIRMED; keine Windows-Mehrklassen-/Controllerfehlerkontrolle |
| TESTER-010 | keine Windows-CRT/Codepage verfügbar; erfolgreiche ASCII-CI-Gates entscheiden den Verdacht nicht | SUSPECTED unverändert |
| TESTER-011 | weiterhin feste gemeinsame Reportziele; serielle CI prüft keine zwei parallelen Sitzungen | SUSPECTED; keine tatsächliche Fehlzuordnung reproduziert |
| TESTER-012 | vorhandener Guard-Schritt success belegt einfache Guard-Gates, nicht äußeren Elternabbruch oder Enkelprozessfreiheit | SUSPECTED; spezifischer Prozessbaumtest fehlt |
| TESTER-013 | keine Mix-FX-Spezifikation oder passende Binärkontrolle verfügbar; Standardfixtures entscheiden host-spezifischen Vertrag nicht | SUSPECTED unverändert |
| TESTER-014 | State-Fixture speichert weiterhin keine nichtstandardmäßigen Werte; CI-Schritt success zeigt API-/Defekterkennung, keine semantische Recallmessung | ENHANCEMENT; Erweiterungsnutzen bleibt begründet |
| TESTER-015 | Setupmatrix versus tatsächliches 48-kHz/512-Processing und bevorzugtes Format erneut abgeglichen | ENHANCEMENT; keine zusätzliche Render-Matrix ausgeführt |
| TESTER-016 | Bypass-/Parameter-Fixtures und Audioorakel fehlen weiterhin; Stille/finite Samples belegen keine Wirkung | ENHANCEMENT; keine Klang-/Wirkungsaussage |
| TESTER-017 | ein Eventblock, acht Paare und 64 Outputplätze unverändert; keine MIDI-fähige CI-Fixture | ENHANCEMENT; Tempo/Transport/NoteOff nicht dynamisch geprüft |
| TESTER-018 | vorhandene Editor-Fixture-Gates laut Einzel-Schrittstatus success; keine gleichzeitige Audio-/Automations-/DAW-Bedienprüfung | ENHANCEMENT; keine Studio-One-GUI-Freigabe |
| TESTER-019 | Durchschnittszeit und schneller stiller Loop weiterhin keine p99-/Deadline-/Callbackallokationsmessung | ENHANCEMENT; keine Echtzeitsicherheitsbehauptung |
| TESTER-020 | alle sieben vorhandenen Fixture-Gates im zusammengefassten Schritt success; SDK-Basis meldet nur Sample32, 64-Bit-Zweige werden nicht angekündigt | ENHANCEMENT; konkrete zusätzliche positive/negative Kontrollen bleiben nötig |
| TESTER-021 | vorhandener Parser-Selbsttest lokal erfolgreich; Artefakt-Digests jetzt sichtbar, Binärhashes und detaillierte Stufen-/Klassenprovenienz weiter offen | ENHANCEMENT; kein Großrefactoring empfohlen |

Erfolgreiche bestehende CI-Gates widerlegen die gefundenen Fehler nicht: Die Kontrollen prüfen gerade nicht alle inaktiven Busse, strenge Handlerpflichten, I/O-Ausfälle, widersprüchlichen Reports oder parallelen Sessions. Zugleich wäre es falsch, aus den statischen Befunden das Versagen aller realen Plugins oder einen generell unbrauchbaren Tester abzuleiten.

### 12.7 Aktualisierte Bereichsbewertung und verbleibende Voraussetzungen

| Prüfbereich | Jetzt tatsächlich belegt | Weiterhin NOT VERIFIED / Hindernis |
| --- | --- | --- |
| aktuelle Quelle / Bericht auf main | Git-SHAs, bytegleiche technische Quelle, PR #3 Merged auf Webseite, Raw-Bericht HTTP 200 und Hash | aktualisierte Fortsetzung muss noch über neuen PR auf main gelangen |
| vorhandene Windows-CI | vier Runs und ihre einzelnen QA-Schritte als success/skipped abgelesen | vollständige geschützte Konsolenlogs, Validator-/Fixture-Einzelzahlen |
| Buildartefakte | vier IDs, Namen, Anzeigegrößen und angezeigte Digests | geschützter Download; keine unabhängige Digest-/Inhalts-/EXE-Prüfung |
| vorhandenes Nutzerpaket | Name vom Nutzer bekannt | konkrete Paketdatei, SHA-256 und Abgleich mit CI-Artefakt fehlen |
| interne Auswertung / Reports | 24 lokale Beobachtungen einschließlich vier reproduzierter Codebefunde und Parserkontrolle | kein vollständiger Windows-Prozess-/GUI-E2E-Lauf |
| Audio-/MIDI-Verarbeitung | statische Prüfung; vorhandene einfache CI-Fixture-Gates laut Schrittstatus erfolgreich | kein lokales Windows-/Pluginbinärartefakt, keine realen Render-/Event-/Transportmessungen |
| VST3-Konformität | gepinnter Vertrag geprüft, bestehender Validator-Selbsttest-Schritt success | keine vollständigen Logs, keine allgemeine Validatorfreigabe für Nutzerplugins |
| State / Automation | API-/Fehler-Fixture-Gates laut Schrittstatus; Quellabdeckung bekannt | semantische Zustands-/Audio-/MIDI-Wiederherstellung und Automationwirkung fehlen |
| GUI-Lifecycle | bestehender Good-/BadEditor-Gateschritt success | keine Win32-Desktop-/Audio-/Skalierungs-/Bedienprüfung durch Auditor |
| Absturzsicherheit | bestehender Produktionsguard-Gateschritt success | äußere Eltern-/Enkelprozesse, Parallelität und spezielle Fehlerpfade nicht nachgewiesen |
| Echtzeit | statische Grenzen der Messung dokumentiert | kein getakteter Echtzeitlauf, p95/p99/max/Overruns/Callbackinstrumentierung |
| Studio One | kein neuer Nachweis | reale Windows-DAW, repräsentative Plugins/Projekte und kontrollierte Bedienprüfung fehlen |
| PR-/Merge-Zugang | Git-Push verfügbar, öffentliche GitHub-Webseiten lesbar | GitHub API/GraphQL weiterhin Proxy-403, keine authentifizierte Web-Schreibsitzung verfügbar |

Die vorhandene Linux-Maschine enthält weder Wine/wine64 noch PowerShell oder eine Windows-Laufzeit. Ein Linux-Extraktionsharness ist hierfür kein Ersatz. Neue Windows-CI wurde nicht gestartet. Für die nächsten Schritte sind ein funktionierender autorisierter API-/Artefaktzugang, vorhandene herunterladbare Logs/Pakete und ein geeigneter Windows-/Studio-One-Prüfplatz erforderlich; keine neuen geheimen Werte im Chat anfordern.

**Gesamtstatus: UNVOLLSTÄNDIG.** Die Fortsetzung hat die CI-Herkunft wesentlich besser belegt und vier Logikbefunde lokal reproduziert. Sie rechtfertigt weder vollständige QA-/FINAL-Freigabe noch Reparaturen ohne separate Freigabe. Die wichtigsten Reparaturprioritäten aus Abschnitt 8 bleiben bestehen; zuerst gültige Hostdaten/Handler und verlässliche Status-/Reportweitergabe.

## 13. Reproduzierbares temporäres Prüfverfahren der Fortsetzung

Das folgende Skript ist ausschließlich Audit-Dokumentation. Es ändert keine Repositorydateien, extrahiert unveränderte Funktionen und schreibt Harness/Ergebnisse unter /tmp. Voraussetzung: lesbarer geprüfter Checkout, GCC/C++17 und die oben dokumentierte SDK-Kopie. Pfade gegebenenfalls an die eigene isolierte Prüfumgebung anpassen. Die erwarteten Beobachtungen umfassen bewusst Fehlerreproduktionen; der Exitcode 0 des Harness bedeutet Übereinstimmung mit diesen Beobachtungen und keine Produktfreigabe.

~~~python
from pathlib import Path
import hashlib, json, subprocess
root=Path('/workspace/125A-Plugin-Tester-Final')
out=Path('/tmp/125a-audit-evidence')
chunks=[]
def part(file,start,end):
    s=(root/file).read_text(); a=s.index(start); b=s.index(end,a)
    text=s[a:b]
    chunks.append({'file':file,'start_line':s[:a].count('\n')+1,'end_line':s[:b].count('\n'),'sha256':hashlib.sha256(text.encode()).hexdigest()})
    return text
headers='''#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "pluginterfaces/vst/ivstaudioprocessor.h"
namespace fs=std::filesystem;
'''
bus=part('src/main.cpp','template <typename Sample>\nstruct TestBusStorage','template <typename Sample>\nbool hasActiveBuffers')
reporter=part('src/main.cpp','enum class Level','std::string toUtf8')
classifier=part('src/dispatcher.cpp','bool readTextFile(const fs::path& path, std::string& text)','std::vector<std::string> extractFindings')
enum=part('src/dispatcher.cpp','enum class ScanStatus','const wchar_t* statusLabel')
gui_path=part('src/gui.cpp','bool isVst3Path','std::wstring readControlText')
gui_status=part('src/gui.cpp','int summaryCount','std::wstring trimLine')
gui_summary=part('src/gui.cpp','std::wstring firstFinding','struct ProgressInfo')
parser=part('src/launcher_diagnostics.cpp','struct ValidatorFailure','bool parseQaCounts')
trim=part('src/launcher.cpp','std::string trimAscii','std::string validatorFailureSummary')
parser_selftest=part('src/launcher_diagnostics.cpp','int runValidatorParserSelfTest()','std::string tresultName')
text=headers+'namespace BusProbe {\nusing namespace Steinberg; using namespace Steinberg::Vst;\nstruct IComponent { bool active; int32 getBusCount(MediaType,BusDirection){return 1;} tresult getBusInfo(MediaType,BusDirection,int32,BusInfo& b){b.channelCount=2;b.flags=active?BusInfo::kDefaultActive:0;return kResultTrue;} };\n'+bus+'}\n'+reporter
text+='namespace ReportPaths { fs::path root; fs::path guard(const fs::path& p){return root/(p.filename().string()+".guard");} fs::path qa(const fs::path& p){return root/(p.filename().string()+".qa");} }\n'
text+=enum+classifier
text+='std::vector<std::pair<std::wstring,std::wstring>> rows; void addResultRow(const std::wstring&a,const std::wstring&b){rows.emplace_back(a,b);}\n'
text+=gui_path+gui_status+gui_summary+trim+parser+parser_selftest
text+=r'''
int total=0, failures=0;
void observe(const std::string& name,bool yes){++total;if(!yes)++failures;std::cout<<(yes?"OBSERVED ":"MISMATCH ")<<name<<'\n';}
void writeQA(const std::string& s){std::ofstream(ReportPaths::qa("test.vst3"))<<s;}
int main(int argc,char**argv){
 if(argc!=2)return 2;ReportPaths::root=argv[1];fs::create_directories(ReportPaths::root);
 BusProbe::IComponent inactive{false},active{true};
 auto i32=BusProbe::makeAudioBuses<Steinberg::Vst::Sample32>(&inactive,Steinberg::Vst::kInput,512);
 auto i64=BusProbe::makeAudioBuses<Steinberg::Vst::Sample64>(&inactive,Steinberg::Vst::kInput,512);
 auto a32=BusProbe::makeAudioBuses<Steinberg::Vst::Sample32>(&active,Steinberg::Vst::kInput,512);
 auto a64=BusProbe::makeAudioBuses<Steinberg::Vst::Sample64>(&active,Steinberg::Vst::kInput,512);
 observe("001 inactive float bus: numChannels=2 and array=null",i32[0].bus.numChannels==2&&i32[0].bus.channelBuffers32==nullptr);
 observe("001 inactive double bus: numChannels=2 and array=null",i64[0].bus.numChannels==2&&i64[0].bus.channelBuffers64==nullptr);
 observe("001 active float control: two allocated buffers",a32[0].bus.channelBuffers32&&a32[0].bus.channelBuffers32[0]&&a32[0].bus.channelBuffers32[1]);
 observe("001 active double control: two allocated buffers",a64[0].bus.channelBuffers64&&a64[0].bus.channelBuffers64[0]&&a64[0].bus.channelBuffers64[1]);
 const std::string good="RESULT: 1 PASS / 0 WARNING / 0 FAIL\n";
 for(int code:{0,1,2,124,125,126,127}){writeQA(good);observe("002 bare PASS summary overrides exit "+std::to_string(code),classifyResult("test.vst3",code)==ScanStatus::Pass);}
 writeQA("RESULT: -2 PASS / -1 WARNING / 0 FAIL\n");observe("002 negative counters accepted as PASS",classifyResult("test.vst3",126)==ScanStatus::Pass);
 writeQA("RESULT: 0 PASS / 0 WARNING / 1 FAIL\n");observe("002 FAIL control",classifyResult("test.vst3",1)==ScanStatus::Fail);
 writeQA("RESULT: 1 PASS / 1 WARNING / 0 FAIL\n");observe("002 WARNING control",classifyResult("test.vst3",0)==ScanStatus::Warning);
 fs::remove(ReportPaths::qa("test.vst3"));observe("002 missing report control is INCONCLUSIVE",classifyResult("test.vst3",0)==ScanStatus::Inconclusive);
 writeQA(good);std::ofstream(ReportPaths::guard("test.vst3"))<<"RESULT: TEST INCONCLUSIVE\n";observe("002 guard control overrides QA",classifyResult("test.vst3",0)==ScanStatus::Inconclusive);
 observe("006 discovery error plus exit1 gives GUI PASS",statusFor("folder",1,L"FOUND: 1\nPASS: 1\nWARNING: 0\nFAIL: 0\nINCONCLUSIVE: 0\nDISCOVERY_ERRORS: 1\n")==L"PASS");
 observe("006 missing plugin report plus exit127 top status is INCONCLUSIVE",statusFor("test.vst3",127,L"")==L"INCONCLUSIVE");
 rows.clear();addPlainLanguageSummary(L"");observe("006 same missing report short explanation is PASS",!rows.empty()&&rows[0].second.rfind(L"PASS",0)==0);
 rows.clear();addPlainLanguageSummary(L"[FAIL] deterministic problem\n");observe("006 FAIL explanation control",!rows.empty()&&rows[0].second.rfind(L"FAIL",0)==0);
 Reporter r;std::ostringstream discard;auto* old=std::cout.rdbuf(discard.rdbuf());r.add(Level::Pass,"large detail",std::string(32768,'X'));std::cout.rdbuf(old);
 observe("007 normal writable report control",r.write(ReportPaths::root/"normal.txt","test.vst3"));
 std::ofstream sink("/dev/full",std::ios::binary);sink<<std::string(32768,'X');sink.flush();observe("007 independent /dev/full write fails",!sink);
 observe("007 Reporter::write nevertheless returns true on /dev/full",r.write("/dev/full","test.vst3"));
 observe("021 existing validator parser selftest",runValidatorParserSelfTest()==0);
 std::cout<<"TOTAL="<<total<<" OBSERVATIONS_MATCHED="<<(total-failures)<<" MISMATCHES="<<failures<<'\n';return failures?1:0;
}
'''
(out/'logic_harness.cpp').write_text(text)
(out/'extracted-functions.json').write_text(json.dumps(chunks,indent=2)+'\n')
compile_cmd=['c++','-std=c++17','-O0','-I/workspace/.onboarding-build/125a/_deps/vst3sdk-src',str(out/'logic_harness.cpp'),'-o',str(out/'logic_harness')]
subprocess.run(compile_cmd,check=True)
r=subprocess.run([str(out/'logic_harness'),str(out/'reports')],text=True,capture_output=True)
(out/'logic-results.txt').write_text(r.stdout+r.stderr)
print(r.stdout,end=''); print(r.stderr,end='')
print('HARNESS_SHA256='+hashlib.sha256(text.encode()).hexdigest())
print('COMPILER='+subprocess.check_output(['c++','-dumpfullversion'],text=True).strip())
raise SystemExit(r.returncode)
~~~

Aufruf: python3 /tmp/125a-audit-evidence/extract_and_test.py, nachdem der Skripttext dort abgelegt wurde. Die erzeugten extrahierten Quellabschnitte sind anhand Datei, Zeilen und SHA-256 in extracted-functions.json identifizierbar. Das Verfahren kann nur den hier benannten Logikumfang reproduzieren.

### Fortsetzungsprotokoll / Statusänderungen

| Datum | Änderung | Begründung |
| --- | --- | --- |
| 2026-10-15, Fortsetzung | Quelle/Erstbericht auf main 12f92a7 bestätigt | PR #3 und HTTP-200-Raw-Abruf plus SHA-256; ursprünglicher Bereitstellungsblocker historisch überholt |
| 2026-10-15, Fortsetzung | CI-Zugänglichkeit teilweise von NOT VERIFIED auf Metadaten verifiziert erweitert | reguläre GitHub-Webseiten liefern vier erfolgreiche Runs und konkrete Schrittstatus trotz gesperrter API |
| 2026-10-15, Fortsetzung | Artefaktmetadaten ergänzt; keine Binärverifikation behauptet | vier IDs/Digests sichtbar, Download bleibt geschützt |
| 2026-10-15, Fortsetzung | TESTER-001/-002/-006/-007: zusätzlicher lokaler Logiknachweis | unverändert extrahierte Funktionen mit Gegenkontrollen; keine Windows-E2E-Hochstufung |
| 2026-10-15, Fortsetzung | TESTER-021: vorhandener Parser-Selbsttest lokal ausgeführt | Rückgabewert 0; kein offizieller Validatorlauf |
| 2026-10-15, Fortsetzung | alle übrigen IDs erhalten und erneut nach Evidenzlage bewertet | keine unberechtigte FIXED-/REJECTED-/APPROVED-Änderung; keine Reparatur |

## 14. Bereitstellungsstand der Fortsetzung

Die Fortsetzung wurde ausschließlich als Änderung dieser Auditdatei auf den Remote-Branch **docs/audit-evidence-continuation-2026-10-15** gepusht. Der vollständige Bericht dieses Branches wurde anschließend von raw.githubusercontent.com mit HTTP 200 abgerufen und byteweise mit der lokalen Fassung verglichen. Alle bestehenden Nicht-Audit-Dateien blieben unverändert.

- [Aktualisierter Bericht auf dem Dokumentationsbranch](https://github.com/challanger2000/125A-Plugin-Tester-Final/blob/docs/audit-evidence-continuation-2026-10-15/AUDIT-REPORT.md)
- [Ausschließlicher Dokumentationsdiff gegen main](https://github.com/challanger2000/125A-Plugin-Tester-Final/compare/main...docs/audit-evidence-continuation-2026-10-15)
- [GitHub-Formular zur PR-Erstellung](https://github.com/challanger2000/125A-Plugin-Tester-Final/pull/new/docs/audit-evidence-continuation-2026-10-15) — kein bereits erstellter PR.

Der ausdrückliche Versuch mit gh pr create für diesen Branch und den Titel „AUDIT: Additional CI and logic evidence for Plugin Tester v0.2.9 [skip ci]“ scheiterte weiterhin mit Post https://api.github.com/graphql: Forbidden. Daher wurde **kein neuer PR erstellt oder zusammengeführt**. Ein direkter Push nach main würde den geforderten PR-Weg umgehen und wurde nicht vorgenommen.

Die Abschlusskontrolle vor dieser Statusergänzung ergab: main weiterhin 12f92a702c4369de1e6d1b2075178a6a47cf4f2d; dort existiert der Erstbericht und ist per HTTP 200 abrufbar, sein SHA-256 ist unverändert f5459f87456291079ac7e8687178dea7ea0469374e2fd4494ad5fafd85d3b6de. Die **Fortsetzung ist noch nicht auf main**. Für den autorisierten reinen Dokumentationsmerge ist ein funktionierender API-/GraphQL-Zugang oder eine autorisierte GitHub-Web-Schreibsitzung erforderlich. [skip ci] beim eventuellen Squash-Merge beibehalten; neue CI-Läufe bleiben nicht freigegeben.
