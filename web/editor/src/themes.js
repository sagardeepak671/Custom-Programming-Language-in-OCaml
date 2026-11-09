// Colour themes for the whole page: the workbench (panels, tabs, status bar, tables - applied
// by web/index.html as CSS variables) and the editor (CodeMirror theme + syntax colours, built
// here). Each theme is written in a compact form - a few base colours plus the syntax palette -
// and `resolveTheme` derives the rest (hover tints, selection, marks, gutters) so the themes
// stay consistent and easy to add. The palettes follow the well-known editor themes of the
// same names (Light+/Dark+/Dark Modern from VS Code, Monokai, Solarized, Dracula, One Dark,
// GitHub, Nord, Dark High Contrast); "MyDSL Light" is the page's original look.
import { EditorView } from '@codemirror/view';
import { HighlightStyle, syntaxHighlighting } from '@codemirror/language';
import { tags } from '@lezer/highlight';

// ui: bg = window background, panel = editor and panel surfaces, panelAlt = headers, tab bar,
// sidebar, table heads; text/muted/border; accent = focus/selection colour; status = status
// bar; error/ok/warn = diagnostics and buttons. syntax: the token colours, also used by the
// tokens table and the AST. editor: optional overrides (bg, fg, gutterFg, selection, cursor).
export const THEMES = [
  { id: 'light', name: 'MyDSL Light', dark: false,
    ui: { bg: '#f6f7f9', panel: '#ffffff', panelAlt: '#f3f4f6', border: '#d8dbe0', text: '#1f2328', muted: '#6b7280', accent: '#2563eb', status: '#2563eb', statusFg: '#ffffff', error: '#b91c1c', ok: '#166534', warn: '#d97706', input: '#fffdf5' },
    syntax: { keyword: '#7c3aed', type: '#0369a1', builtin: '#0f766e', number: '#b45309', string: '#15803d', comment: '#6b7280', variable: '#1f2328', operator: '#1f2328', bracket: '#1f2328', invalid: '#b91c1c' },
    editor: { bg: '#ffffff', fg: '#1f2328', gutterBg: '#fafafa', gutterFg: '#9ca3af', selection: '#bfdbfe', activeLine: 'rgba(37, 99, 235, 0.045)' } },
  { id: 'vs-light', name: 'Light+ (VS Code)', dark: false,
    ui: { bg: '#f3f3f3', panel: '#ffffff', panelAlt: '#f3f3f3', border: '#e5e5e5', text: '#3b3b3b', muted: '#717171', accent: '#005fb8', status: '#007acc', statusFg: '#ffffff', error: '#e51400', ok: '#388a34', warn: '#bf8803', input: '#ffffff' },
    syntax: { keyword: '#af00db', type: '#267f99', builtin: '#795e26', number: '#098658', string: '#a31515', comment: '#008000', variable: '#001080', operator: '#000000', bracket: '#0431fa', invalid: '#cd3131' },
    editor: { bg: '#ffffff', fg: '#3b3b3b', gutterBg: '#ffffff', gutterFg: '#6e7681', selection: '#add6ff', activeLine: 'rgba(0, 0, 0, 0.04)' } },
  { id: 'vs-dark', name: 'Dark+ (VS Code)', dark: true,
    ui: { bg: '#252526', panel: '#1e1e1e', panelAlt: '#252526', border: '#3c3c3c', text: '#d4d4d4', muted: '#8b8b8b', accent: '#0e639c', status: '#007acc', statusFg: '#ffffff', error: '#f14c4c', ok: '#4ec94e', warn: '#cca700', input: '#1e1e1e' },
    syntax: { keyword: '#c586c0', type: '#4ec9b0', builtin: '#dcdcaa', number: '#b5cea8', string: '#ce9178', comment: '#6a9955', variable: '#9cdcfe', operator: '#d4d4d4', bracket: '#ffd700', invalid: '#f44747' },
    editor: { bg: '#1e1e1e', fg: '#d4d4d4', gutterBg: '#1e1e1e', gutterFg: '#858585', selection: '#264f78', activeLine: 'rgba(255, 255, 255, 0.04)', cursor: '#aeafad' } },
  { id: 'dark-modern', name: 'Dark Modern (VS Code)', dark: true,
    ui: { bg: '#181818', panel: '#1f1f1f', panelAlt: '#181818', border: '#2b2b2b', text: '#cccccc', muted: '#9d9d9d', accent: '#0078d4', status: '#181818', statusFg: '#cccccc', error: '#f85149', ok: '#2ea043', warn: '#cca700', input: '#1f1f1f' },
    syntax: { keyword: '#c586c0', type: '#4ec9b0', builtin: '#dcdcaa', number: '#b5cea8', string: '#ce9178', comment: '#6a9955', variable: '#9cdcfe', operator: '#d4d4d4', bracket: '#ffd700', invalid: '#f44747' },
    editor: { bg: '#1f1f1f', fg: '#cccccc', gutterBg: '#1f1f1f', gutterFg: '#6e7681', selection: '#264f78', activeLine: 'rgba(255, 255, 255, 0.04)', cursor: '#aeafad' } },
  { id: 'monokai', name: 'Monokai', dark: true,
    ui: { bg: '#1e1f1c', panel: '#272822', panelAlt: '#1e1f1c', border: '#414339', text: '#f8f8f2', muted: '#90908a', accent: '#a6e22e', status: '#414339', statusFg: '#f8f8f2', error: '#f92672', ok: '#a6e22e', warn: '#e6db74', input: '#272822' },
    syntax: { keyword: '#f92672', type: '#66d9ef', builtin: '#a6e22e', number: '#ae81ff', string: '#e6db74', comment: '#75715e', variable: '#f8f8f2', operator: '#f92672', bracket: '#f8f8f2', invalid: '#f92672' },
    editor: { bg: '#272822', fg: '#f8f8f2', gutterBg: '#272822', gutterFg: '#90908a', selection: '#49483e', activeLine: '#3e3d32', cursor: '#f8f8f0' } },
  { id: 'solarized-light', name: 'Solarized Light', dark: false,
    ui: { bg: '#eee8d5', panel: '#fdf6e3', panelAlt: '#eee8d5', border: '#d9d2c2', text: '#586e75', muted: '#93a1a1', accent: '#268bd2', status: '#eee8d5', statusFg: '#586e75', error: '#dc322f', ok: '#859900', warn: '#b58900', input: '#fdf6e3' },
    syntax: { keyword: '#859900', type: '#b58900', builtin: '#268bd2', number: '#d33682', string: '#2aa198', comment: '#93a1a1', variable: '#586e75', operator: '#657b83', bracket: '#586e75', invalid: '#dc322f' },
    editor: { bg: '#fdf6e3', fg: '#657b83', gutterBg: '#eee8d5', gutterFg: '#93a1a1', selection: '#eee8d5', activeLine: 'rgba(0, 0, 0, 0.04)', cursor: '#657b83' } },
  { id: 'solarized-dark', name: 'Solarized Dark', dark: true,
    ui: { bg: '#00212b', panel: '#002b36', panelAlt: '#073642', border: '#0e4955', text: '#93a1a1', muted: '#657b83', accent: '#268bd2', status: '#073642', statusFg: '#93a1a1', error: '#dc322f', ok: '#859900', warn: '#b58900', input: '#002b36' },
    syntax: { keyword: '#859900', type: '#b58900', builtin: '#268bd2', number: '#d33682', string: '#2aa198', comment: '#586e75', variable: '#93a1a1', operator: '#839496', bracket: '#93a1a1', invalid: '#dc322f' },
    editor: { bg: '#002b36', fg: '#839496', gutterBg: '#073642', gutterFg: '#586e75', selection: '#073642', activeLine: 'rgba(255, 255, 255, 0.04)', cursor: '#839496' } },
  { id: 'dracula', name: 'Dracula', dark: true,
    ui: { bg: '#21222c', panel: '#282a36', panelAlt: '#21222c', border: '#44475a', text: '#f8f8f2', muted: '#6272a4', accent: '#bd93f9', status: '#191a21', statusFg: '#f8f8f2', error: '#ff5555', ok: '#50fa7b', warn: '#ffb86c', input: '#282a36' },
    syntax: { keyword: '#ff79c6', type: '#8be9fd', builtin: '#50fa7b', number: '#bd93f9', string: '#f1fa8c', comment: '#6272a4', variable: '#f8f8f2', operator: '#ff79c6', bracket: '#f8f8f2', invalid: '#ff5555' },
    editor: { bg: '#282a36', fg: '#f8f8f2', gutterBg: '#282a36', gutterFg: '#6272a4', selection: '#44475a', activeLine: 'rgba(255, 255, 255, 0.05)', cursor: '#f8f8f2' } },
  { id: 'one-dark', name: 'One Dark Pro', dark: true,
    ui: { bg: '#21252b', panel: '#282c34', panelAlt: '#21252b', border: '#181a1f', text: '#abb2bf', muted: '#6b717d', accent: '#528bff', status: '#21252b', statusFg: '#9da5b4', error: '#e06c75', ok: '#98c379', warn: '#e5c07b', input: '#282c34' },
    syntax: { keyword: '#c678dd', type: '#e5c07b', builtin: '#61afef', number: '#d19a66', string: '#98c379', comment: '#5c6370', variable: '#e06c75', operator: '#56b6c2', bracket: '#abb2bf', invalid: '#e06c75' },
    editor: { bg: '#282c34', fg: '#abb2bf', gutterBg: '#282c34', gutterFg: '#4b5263', selection: '#3e4451', activeLine: '#2c313c', cursor: '#528bff' } },
  { id: 'github-light', name: 'GitHub Light', dark: false,
    ui: { bg: '#f6f8fa', panel: '#ffffff', panelAlt: '#f6f8fa', border: '#d0d7de', text: '#1f2328', muted: '#656d76', accent: '#0969da', status: '#ffffff', statusFg: '#1f2328', error: '#cf222e', ok: '#1a7f37', warn: '#9a6700', input: '#ffffff' },
    syntax: { keyword: '#cf222e', type: '#953800', builtin: '#8250df', number: '#0550ae', string: '#0a3069', comment: '#6e7781', variable: '#1f2328', operator: '#0550ae', bracket: '#1f2328', invalid: '#cf222e' },
    editor: { bg: '#ffffff', fg: '#1f2328', gutterBg: '#ffffff', gutterFg: '#8c959f', selection: '#b6d4fe', activeLine: 'rgba(234, 238, 242, 0.5)', cursor: '#1f2328' } },
  { id: 'github-dark', name: 'GitHub Dark', dark: true,
    ui: { bg: '#010409', panel: '#0d1117', panelAlt: '#010409', border: '#30363d', text: '#e6edf3', muted: '#8b949e', accent: '#2f81f7', status: '#010409', statusFg: '#e6edf3', error: '#f85149', ok: '#3fb950', warn: '#d29922', input: '#0d1117' },
    syntax: { keyword: '#ff7b72', type: '#ffa657', builtin: '#d2a8ff', number: '#79c0ff', string: '#a5d6ff', comment: '#8b949e', variable: '#e6edf3', operator: '#79c0ff', bracket: '#e6edf3', invalid: '#f85149' },
    editor: { bg: '#0d1117', fg: '#e6edf3', gutterBg: '#0d1117', gutterFg: '#6e7681', selection: '#264f78', activeLine: 'rgba(110, 118, 129, 0.1)', cursor: '#e6edf3' } },
  { id: 'nord', name: 'Nord', dark: true,
    ui: { bg: '#2e3440', panel: '#2e3440', panelAlt: '#3b4252', border: '#434c5e', text: '#d8dee9', muted: '#7b88a1', accent: '#88c0d0', status: '#3b4252', statusFg: '#d8dee9', error: '#bf616a', ok: '#a3be8c', warn: '#ebcb8b', input: '#2e3440' },
    syntax: { keyword: '#81a1c1', type: '#8fbcbb', builtin: '#88c0d0', number: '#b48ead', string: '#a3be8c', comment: '#616e88', variable: '#d8dee9', operator: '#81a1c1', bracket: '#eceff4', invalid: '#bf616a' },
    editor: { bg: '#2e3440', fg: '#d8dee9', gutterBg: '#2e3440', gutterFg: '#4c566a', selection: '#434c5e', activeLine: '#3b4252', cursor: '#d8dee9' } },
  { id: 'high-contrast', name: 'Dark High Contrast', dark: true,
    ui: { bg: '#000000', panel: '#000000', panelAlt: '#000000', border: '#6fc3df', text: '#ffffff', muted: '#a5a5a5', accent: '#f38518', status: '#000000', statusFg: '#ffffff', error: '#f48771', ok: '#89d185', warn: '#ffd700', input: '#000000' },
    syntax: { keyword: '#569cd6', type: '#4ec9b0', builtin: '#dcdcaa', number: '#b5cea8', string: '#ce9178', comment: '#7ca668', variable: '#9cdcfe', operator: '#d4d4d4', bracket: '#ffd700', invalid: '#f44747' },
    editor: { bg: '#000000', fg: '#ffffff', gutterBg: '#000000', gutterFg: '#ffffff', selection: '#ffffff40', activeLine: 'rgba(255, 255, 255, 0.08)', cursor: '#ffffff' } },
];
export const DEFAULT_THEME = 'light';

// "#rrggbb" (or "#rrggbbaa") with the given opacity, as an rgba() string.
export function alpha(hex, opacity) {
  const m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})/i.exec(hex);
  if (!m) return hex;
  return `rgba(${parseInt(m[1], 16)}, ${parseInt(m[2], 16)}, ${parseInt(m[3], 16)}, ${opacity})`;
}

// Black or white, whichever reads better on `hex`.
export function contrastText(hex) {
  const m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})/i.exec(hex);
  if (!m) return '#ffffff';
  const [r, g, b] = [m[1], m[2], m[3]].map(x => parseInt(x, 16) / 255);
  return 0.2126 * r + 0.7152 * g + 0.0722 * b > 0.55 ? '#000000' : '#ffffff';
}

// The complete palette of a theme: everything the page and the editor need, derived from the
// compact definition. Unknown ids fall back to the default theme.
export function resolveTheme(id) {
  const theme = THEMES.find(t => t.id === id) || THEMES.find(t => t.id === DEFAULT_THEME);
  const { ui, syntax, editor = {} } = theme, dark = theme.dark;
  const tint = dark ? '#ffffff' : '#000000';
  const full = {
    id: theme.id, name: theme.name, dark,
    ui: {
      ...ui,
      accentBg: alpha(ui.accent, dark ? 0.28 : 0.16),   // selected rows, pressed toggles, cursor node
      hover: alpha(tint, dark ? 0.08 : 0.05),
      errorBg: alpha(ui.error, dark ? 0.2 : 0.12),
      errorBorder: alpha(ui.error, 0.5),
      okBg: alpha(ui.ok, dark ? 0.22 : 0.14),
      warnBg: alpha(ui.warn, dark ? 0.22 : 0.16),
      cursorBg: alpha(ui.warn, dark ? 0.28 : 0.25),      // the AST node under the cursor
      rowBorder: alpha(tint, dark ? 0.07 : 0.05),
      buttonBg: dark ? alpha('#ffffff', 0.06) : '#ffffff',
      okFg: contrastText(ui.ok), accentFg: contrastText(ui.accent),
      shadow: alpha('#000000', dark ? 0.5 : 0.15),
    },
    syntax: { ...syntax, trace: ui.warn },
    editor: {
      bg: editor.bg || ui.panel, fg: editor.fg || ui.text,
      gutterBg: editor.gutterBg || ui.panel, gutterFg: editor.gutterFg || ui.muted,
      selection: editor.selection || alpha(ui.accent, 0.3),
      activeLine: editor.activeLine || alpha(tint, 0.05),
      cursor: editor.cursor || (editor.fg || ui.text),
      selectionMatch: alpha(ui.warn, 0.22),
      matchBracket: alpha(ui.ok, 0.25), matchBracketOutline: ui.ok,
      nonmatchBracket: alpha(ui.error, 0.25), nonmatchBracketOutline: ui.error,
      markSelected: alpha(ui.accent, dark ? 0.32 : 0.18), markSelectedOutline: ui.accent,
      markError: alpha(ui.error, dark ? 0.3 : 0.15), markErrorOutline: ui.error,
      markTrace: alpha(ui.warn, dark ? 0.3 : 0.2), markTraceOutline: ui.warn,
      searchMatch: alpha(ui.warn, 0.4), searchMatchSelected: alpha(ui.warn, 0.7),
      snippetField: alpha(ui.warn, 0.22), snippetFieldOutline: ui.warn,
      panelBg: ui.panelAlt, tooltipBg: ui.panel, tooltipBorder: ui.border,
      completionSelected: alpha(ui.accent, dark ? 0.35 : 0.18),
    },
  };
  return full;
}

// The editor's part of a theme: CodeMirror theme + syntax highlighting.
export function editorTheme(theme) {
  const e = theme.editor, s = theme.syntax, ui = theme.ui;
  const mono = 'ui-monospace, SFMono-Regular, Menlo, Consolas, monospace';
  const view = EditorView.theme({
    '&': { height: '100%', fontSize: '13px', backgroundColor: e.bg, color: e.fg },
    '.cm-scroller': { fontFamily: mono, lineHeight: '20px', overflow: 'auto' },
    '.cm-content': { padding: '8px 0', caretColor: e.cursor },
    '.cm-line': { padding: '0 10px' },
    '.cm-gutters': { backgroundColor: e.gutterBg, color: e.gutterFg, borderRight: `1px solid ${ui.border}`, minWidth: '40px' },
    '.cm-lineNumbers .cm-gutterElement': { padding: '0 8px 0 12px' },
    '.cm-activeLineGutter': { backgroundColor: e.activeLine, color: e.fg },
    '.cm-activeLine': { backgroundColor: e.activeLine },
    '&.cm-focused': { outline: 'none' },
    '&.cm-focused .cm-cursor, .cm-cursor': { borderLeftColor: e.cursor },
    '.cm-selectionBackground, &.cm-focused .cm-selectionBackground, ::selection': { backgroundColor: `${e.selection} !important` },
    '.cm-selectionMatch': { backgroundColor: e.selectionMatch },
    '.cm-matchingBracket, &.cm-focused .cm-matchingBracket': { backgroundColor: e.matchBracket, outline: `1px solid ${e.matchBracketOutline}`, color: 'inherit' },
    '.cm-nonmatchingBracket, &.cm-focused .cm-nonmatchingBracket': { backgroundColor: e.nonmatchBracket, outline: `1px solid ${e.nonmatchBracketOutline}`, color: 'inherit' },
    '.cm-mark': { borderRadius: '2px' },
    '.cm-mark-selected': { backgroundColor: e.markSelected, outline: `1px solid ${e.markSelectedOutline}` },
    '.cm-mark-error': { backgroundColor: e.markError, outline: `2px solid ${e.markErrorOutline}` },
    '.cm-mark-trace': { backgroundColor: e.markTrace, outline: `1px dashed ${e.markTraceOutline}` },
    '.cm-mark-end': { display: 'inline-block', width: '0.6em' },
    '.cm-panels': { backgroundColor: e.panelBg, color: e.fg, borderColor: ui.border },
    '.cm-panels.cm-panels-bottom': { borderTop: `1px solid ${ui.border}` },
    '.cm-panel input, .cm-panel button': { backgroundColor: e.bg, color: e.fg, border: `1px solid ${ui.border}`, borderRadius: '3px' },
    '.cm-searchMatch': { backgroundColor: e.searchMatch, outline: `1px solid ${ui.warn}` },
    '.cm-searchMatch.cm-searchMatch-selected': { backgroundColor: e.searchMatchSelected },
    '.cm-tooltip': { border: `1px solid ${e.tooltipBorder}`, backgroundColor: e.tooltipBg, color: e.fg, boxShadow: `0 4px 12px ${ui.shadow}` },
    '.cm-tooltip.cm-tooltip-autocomplete > ul': { fontFamily: mono, fontSize: '12px', maxHeight: '16em' },
    '.cm-tooltip.cm-tooltip-autocomplete > ul > li': { padding: '2px 6px', lineHeight: '1.5' },
    '.cm-tooltip.cm-tooltip-autocomplete > ul > li[aria-selected]': { backgroundColor: e.completionSelected, color: e.fg },
    '.cm-completionDetail': { color: ui.muted, fontStyle: 'normal', marginLeft: '0.8em' },
    '.cm-completionIcon': { width: '1.2em', opacity: '0.8' },
    '.cm-completionIcon-keyword::after': { color: s.keyword }, '.cm-completionIcon-type::after': { color: s.type },
    '.cm-completionIcon-function::after': { color: s.builtin }, '.cm-completionIcon-variable::after': { color: s.variable },
    '.cm-completionIcon-constant::after': { color: s.number },
    '.cm-tooltip.cm-completionInfo': { padding: '6px 8px', maxWidth: '30em', fontSize: '12px', lineHeight: '1.45', color: e.fg },
    '.cm-mydsl-info code': { display: 'block', fontFamily: mono, color: s.type, marginBottom: '4px', whiteSpace: 'pre-wrap' },
    '.cm-snippetField': { backgroundColor: e.snippetField, outline: `1px solid ${e.snippetFieldOutline}`, borderRadius: '2px' },
  }, { dark: theme.dark });
  const highlight = HighlightStyle.define([
    { tag: tags.keyword, color: s.keyword, fontWeight: 'bold' },
    { tag: tags.typeName, color: s.type, fontWeight: 'bold' },
    { tag: tags.bool, color: s.number, fontWeight: 'bold' },
    { tag: tags.standard(tags.variableName), color: s.builtin },
    { tag: tags.function(tags.variableName), color: s.builtin },
    { tag: tags.variableName, color: s.variable },
    { tag: tags.number, color: s.number },
    { tag: tags.string, color: s.string },
    { tag: tags.comment, color: s.comment, fontStyle: 'italic' },
    { tag: tags.operator, color: s.operator },
    { tag: tags.bracket, color: s.bracket },
    { tag: tags.punctuation, color: s.operator },
    { tag: tags.invalid, color: s.invalid, textDecoration: `underline wavy ${s.invalid}` },
  ]);
  return [view, syntaxHighlighting(highlight)];
}
