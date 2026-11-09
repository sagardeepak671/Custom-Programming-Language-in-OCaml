// The MyDSL source editor: CodeMirror 6 configured for the language, exposed to web/index.html
// as `window.MyDSLEditor.create(...)`. The page only uses the small `Editor` interface below
// (offsets into the text, marks, cursor), so tests/web.test.js can replace it with a stub.
//
// Built by `npm run build` in web/editor into web/vendor/mydsl-editor.js, which the mydsl
// binary embeds and serves itself (no CDN, works offline).
import { EditorState, StateField, StateEffect, Prec, Compartment } from '@codemirror/state';
import { EditorView, keymap, lineNumbers, highlightActiveLine, highlightActiveLineGutter, highlightSpecialChars,
         drawSelection, Decoration, WidgetType, rectangularSelection, crosshairCursor } from '@codemirror/view';
import { defaultKeymap, history, historyKeymap, indentWithTab, undo, redo } from '@codemirror/commands';
import { bracketMatching, indentOnInput } from '@codemirror/language';
import { closeBrackets, closeBracketsKeymap, autocompletion, acceptCompletion, startCompletion, snippet, completionStatus } from '@codemirror/autocomplete';
import { syntaxTree } from '@codemirror/language';
import { searchKeymap, highlightSelectionMatches } from '@codemirror/search';
import { mydsl, SNIPPETS, KEYWORDS, TYPE_KEYWORDS, CONSTANTS, BUILTINS } from './mydsl-language.js';
import { THEMES, DEFAULT_THEME, resolveTheme, editorTheme } from './themes.js';

// ---- marks: highlight ranges the page sets (selected token/node, error position, call sites) ----
const setMarks = StateEffect.define();
// A zero-width mark (an error at the end of the input) is drawn as a small box after the text.
class EndMarker extends WidgetType {
  constructor(cls) { super(); this.cls = cls; }
  eq(other) { return other.cls === this.cls; }
  toDOM() { const span = document.createElement('span'); span.className = `cm-mark ${this.cls} cm-mark-end`; span.textContent = '\u00a0'; return span; }
  ignoreEvent() { return false; }
}
const marksField = StateField.define({
  create: () => Decoration.none,
  update(marks, tr) {
    marks = marks.map(tr.changes);
    for (const effect of tr.effects) if (effect.is(setMarks)) marks = buildMarks(effect.value, tr.state.doc.length);
    return marks;
  },
  provide: field => EditorView.decorations.from(field),
});
function buildMarks(ranges, length) {
  const decorations = [];
  for (const { from, to, cls } of ranges) {
    const a = Math.max(0, Math.min(from, length)), b = Math.max(a, Math.min(to, length));
    if (b > a) decorations.push(Decoration.mark({ class: `cm-mark cm-mark-${cls}` }).range(a, b));
    else decorations.push(Decoration.widget({ widget: new EndMarker(`cm-mark-${cls}`), side: 1 }).range(a));
  }
  decorations.sort((x, y) => x.from - y.from || x.startSide - y.startSide);
  return Decoration.set(decorations, true);
}

// ---- completion ----
// The page supplies the symbols (`completions()` returns an array; see web/index.html
// `symbols()`): {label, kind, detail, info, signature, params, template, fromTokens}. kind is
// 'keyword' | 'type' | 'constant' | 'builtin' | 'function' | 'variable' | 'parameter' | 'name'
// ('name' = a word of the current text that the C++ lexer produced, for code that does not
// parse yet). Nothing here knows the language: keywords, builtins and their descriptions come
// from the C++ side through the page.
const completionTypes = { keyword: 'keyword', type: 'type', constant: 'constant', builtin: 'function', function: 'function', variable: 'variable', parameter: 'variable', name: 'text' };
const completionBoost = { function: 3, variable: 3, parameter: 3, builtin: 2, keyword: 1, type: 1, constant: 1, name: 0 };
const wordRe = /[A-Za-z_'][A-Za-z0-9_']*/;
// The parameter names of a builtin's signature "substring(s, start, count)" -> ["s","start","count"].
function signatureParams(signature) {
  const inner = /\(([^)]*)\)/.exec(signature.split(' or ')[0]);
  return inner && inner[1].trim() ? inner[1].split(',').map(p => p.trim()) : [];
}
function callSnippet(name, params) {
  return snippet(`${name}(${params.map((p, i) => `\${${i + 1}:${p}}`).join(', ')})`);
}
// The info box of a completion: the signature/template in monospace, the description below.
function infoBox(symbol) {
  const first = symbol.kind === 'builtin' && symbol.signature ? `${symbol.signature}${symbol.detail ? ': ' + symbol.detail : ''}`
              : symbol.template || (symbol.kind === 'function' && symbol.detail ? `${symbol.label}${symbol.detail}` : '');
  if (!first && !symbol.info) return null;
  const box = document.createElement('div');
  box.className = 'cm-mydsl-info';
  if (first) { const code = document.createElement('code'); code.textContent = first; box.appendChild(code); }
  if (symbol.info) { const text = document.createElement('div'); text.textContent = symbol.info; box.appendChild(text); }
  return box;
}
function toCompletion(symbol) {
  const completion = {
    label: symbol.label,
    type: completionTypes[symbol.kind] || 'text',
    boost: completionBoost[symbol.kind] || 0,
    detail: symbol.kind === 'builtin' && symbol.signature ? symbol.signature.split(' or ')[0].slice(symbol.label.length) : symbol.detail || undefined,
    info: () => infoBox(symbol),
  };
  if (symbol.kind === 'keyword' && SNIPPETS[symbol.label]) completion.apply = snippet(SNIPPETS[symbol.label]);
  else if (symbol.kind === 'function') completion.apply = callSnippet(symbol.label, symbol.params || []);
  else if (symbol.kind === 'builtin' && symbol.signature) completion.apply = callSnippet(symbol.label, signatureParams(symbol.signature));
  return completion;
}
// Inside a comment or a string literal nothing is suggested.
function inCommentOrString(state, pos) {
  const node = syntaxTree(state).resolveInner(pos, -1);
  return /comment|string/i.test(node.name);
}
function completionSource(completions) {
  return context => {
    const word = context.matchBefore(wordRe);
    if (!word && !context.explicit) return null;
    if (inCommentOrString(context.state, context.pos)) return null;
    const typed = word ? word.text : '';
    const options = [];
    for (const symbol of completions() || []) {
      if (symbol.fromTokens && symbol.label === typed) continue; // the word being typed is no suggestion for itself
      options.push(toCompletion(symbol));
    }
    return { from: word ? word.from : context.pos, options, validFor: /^[A-Za-z0-9_']*$/ };
  };
}

// ---- theme: see themes.js (one palette for the page and the editor; swapped at run time) ----
const themeCompartment = new Compartment();

// ---- the editor object handed to the page ----
function create({ parent, doc = '', onChange = () => {}, onCursor = () => {}, onParse = () => {}, onRun = () => {}, onSave = () => {}, completions = () => [] }) {
  const readOnly = new Compartment();
  // One EditorState per open file: the page swaps them with getState/setState when the user
  // switches tabs, so every file keeps its own undo history, cursor and scroll position.
  const extensions = [
    Prec.highest(keymap.of([
      { key: 'Mod-Enter', run: () => { onParse(); return true; } },
      { key: 'Mod-Shift-Enter', run: () => { onRun(); return true; } },
      { key: 'Mod-s', run: () => { onSave(); return true; } },
      { key: 'Tab', run: acceptCompletion }, // Enter accepts too (completionKeymap); Tab otherwise indents
    ])),
    autocompletion({ override: [completionSource(completions)], activateOnTyping: true, maxRenderedOptions: 40, defaultKeymap: true }),
    lineNumbers(), highlightActiveLineGutter(), highlightSpecialChars(), history(), drawSelection(),
    EditorState.allowMultipleSelections.of(true), rectangularSelection(), crosshairCursor(),
    indentOnInput(), bracketMatching(), closeBrackets(), highlightActiveLine(), highlightSelectionMatches(),
    keymap.of([...closeBracketsKeymap, ...defaultKeymap, ...searchKeymap, ...historyKeymap, indentWithTab]),
    EditorState.tabSize.of(4),
    mydsl(), marksField,
    themeCompartment.of(editorTheme(resolveTheme(DEFAULT_THEME))),
    EditorView.updateListener.of(update => {
      if (update.docChanged) onChange();
      if (update.docChanged || update.selectionSet) onCursor();
    }),
  ];
  let currentTheme = DEFAULT_THEME;
  const createState = (text, readOnlyFlag = false) => EditorState.create({ doc: text, extensions: [extensions, readOnly.of(EditorState.readOnly.of(readOnlyFlag))] });
  const view = new EditorView({ parent, state: createState(doc) });
  const applyTheme = () => view.dispatch({ effects: themeCompartment.reconfigure(editorTheme(resolveTheme(currentTheme))) });
  const clamp = offset => Math.max(0, Math.min(offset | 0, view.state.doc.length));
  const editor = {
    view,
    getValue: () => view.state.doc.toString(),
    // Replaces the whole text (undoable) and moves the cursor to the start.
    setValue(text) {
      view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: text }, selection: { anchor: 0 }, scrollIntoView: true });
    },
    getCursor: () => view.state.selection.main.head,
    // Moves the cursor to `offset` and scrolls it into view (only if needed, keeping a margin).
    setCursor(offset) {
      const pos = clamp(offset);
      view.dispatch({ selection: { anchor: pos }, effects: EditorView.scrollIntoView(pos, { y: 'nearest', yMargin: 40 }) });
    },
    scrollTo(offset) { view.dispatch({ effects: EditorView.scrollIntoView(clamp(offset), { y: 'nearest', yMargin: 40 }) }); },
    // ranges: [{from, to, cls}] with cls 'selected' | 'error' | 'trace'; replaces the previous marks.
    setMarks(ranges) { view.dispatch({ effects: setMarks.of(ranges) }); },
    focus: () => view.focus(),
    hasFocus: () => view.hasFocus,
    undo: () => undo(view), redo: () => redo(view),
    // Opens the completion list (Ctrl+Space does the same); `completionOpen` tells whether it is shown.
    complete: () => startCompletion(view),
    completionOpen: () => completionStatus(view.state) === 'active',
    lineCount: () => view.state.doc.lines,
    setReadOnly(flag) { view.dispatch({ effects: readOnly.reconfigure(EditorState.readOnly.of(!!flag)) }); },
    isReadOnly: () => view.state.readOnly,
    // Per-file states (see above): createState makes a fresh one for a text, getState/setState
    // swap the state shown. setState fires no onChange; the page syncs itself afterwards.
    createState, getState: () => view.state, setState(state) { view.setState(state); applyTheme(); }, // every stored state gets the current theme
    // Themes (themes.js): setTheme(id) restyles the editor; the page applies the same palette to itself.
    setTheme(id) { currentTheme = THEMES.some(th => th.id === id) ? id : DEFAULT_THEME; applyTheme(); return currentTheme; },
    theme: () => currentTheme,
    destroy: () => view.destroy(),
  };
  return editor;
}

const MyDSLEditor = { create, language: { KEYWORDS, TYPE_KEYWORDS, CONSTANTS, BUILTINS }, themes: THEMES.map(th => ({ id: th.id, name: th.name, dark: th.dark })), resolveTheme, defaultTheme: DEFAULT_THEME, EditorView, EditorState };
window.MyDSLEditor = MyDSLEditor;
export default MyDSLEditor;
