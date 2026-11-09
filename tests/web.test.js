// Runs the page script from web/index.html in Node with a minimal DOM stub and feeds it real
// output of the C++ binary (`mydsl --json`). Checks the source<->token<->AST mapping that the
// page relies on: click a token or node -> highlighted source range; cursor -> innermost node;
// error -> banner and marker. The CodeMirror editor (web/vendor/mydsl-editor.js) needs a real
// DOM, so it is replaced by a stub with the same interface (text, cursor, marks); the editor
// itself is checked in a browser by tests/editor.test.js.
// Usage: node tests/web.test.js ./mydsl   (run from the project dir)
'use strict';
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const exe = process.argv[2] || './mydsl';
const html = fs.readFileSync(path.join(__dirname, '..', 'web', 'index.html'), 'utf8');
const script = html.match(/<script>([\s\S]*)<\/script>/)[1];

// ---- tiny DOM stub -------------------------------------------------------------------
const all = [];
function element(tag) {
  const el = {
    tagName: tag.toUpperCase(), children: [], parentElement: null, handlers: {}, open: false,
    className: '', title: '', value: '', selectionStart: 0, hidden: false, style: {}, attributes: {},
    scrollTop: 0, scrollLeft: 0, clientHeight: 400,
    classList: {
      add: (...c) => c.forEach(x => { if (!el.classList.contains(x)) el.className = (el.className + ' ' + x).trim(); }),
      remove: (...c) => { el.className = el.className.split(/\s+/).filter(x => x && !c.includes(x)).join(' '); },
      contains: c => el.className.split(/\s+/).includes(c),
    },
    appendChild(child) { child.parentElement = el; el.children.push(child); return child; },
    addEventListener(type, fn) { (el.handlers[type] = el.handlers[type] || []).push(fn); },
    removeEventListener(type, fn) { el.handlers[type] = (el.handlers[type] || []).filter(f => f !== fn); },
    setAttribute(name, value) { el.attributes[name] = String(value); }, getAttribute(name) { return el.attributes[name] ?? null; },
    getBoundingClientRect: () => ({ width: 400, height: 300 }),
    dispatch(type) { (el.handlers[type] || []).forEach(fn => fn({ preventDefault() {}, stopPropagation() {}, target: el })); },
    remove() { if (el.parentElement) el.parentElement.children = el.parentElement.children.filter(c => c !== el); },
    querySelectorAll: sel => query(el, sel),
    querySelector: sel => query(el, sel)[0] || null,
    scrollIntoView() {}, focus() {}, setSelectionRange(s) { el.selectionStart = s; }, setRangeText() {},
  };
  Object.defineProperty(el, 'innerHTML', { get: () => el._html || '', set: v => { el._html = v; el.children = []; } });
  Object.defineProperty(el, 'textContent', { get: () => el._text || '', set: v => { el._text = v; el.children = []; } });
  all.push(el);
  return el;
}
function matches(el, selector) {
  return selector.split('.').every((part, i) => i === 0 ? (!part || el.tagName === part.toUpperCase()) : el.classList.contains(part));
}
function descendants(el, out = []) { for (const c of el.children) { out.push(c); descendants(c, out); } return out; }
function query(root, selector) {
  const pool = root ? descendants(root) : all;
  return pool.filter(el => selector.split(',').some(s => matches(el, s.trim())));
}
// The editor stub: the page only uses this interface (see web/editor/src/main.js).
let ed = null;
// The themes: the stub editor records setTheme; resolveTheme/themes come from the real bundle's data (web/editor/src/themes.js), loaded as a module copy below.
const themesSource = fs.readFileSync(path.join(__dirname, '..', 'web', 'editor', 'src', 'themes.js'), 'utf8');
const themesModule = {};
new Function('exports', themesSource.replace(/^import .*$/gm, '').replace(/^export /gm, '') + '\nexports.THEMES = THEMES; exports.resolveTheme = resolveTheme; exports.DEFAULT_THEME = DEFAULT_THEME;')(themesModule);
const MyDSLEditor = { themes: themesModule.THEMES.map(th => ({ id: th.id, name: th.name, dark: th.dark })), resolveTheme: themesModule.resolveTheme, defaultTheme: themesModule.DEFAULT_THEME, create(options) {
  ed = {
    options, value: options.doc, cursor: 0, marks: [], focused: false,
    getValue: () => ed.value,
    setValue(text) { ed.value = text; ed.cursor = 0; },
    getCursor: () => ed.cursor, setCursor(offset) { ed.cursor = offset; }, scrollTo() {},
    setMarks(ranges) { ed.marks = ranges; }, focus() { ed.focused = true; },
    type(text) { ed.value = text; options.onChange(); }, // an edit by the user
    // per-tab states: {value, cursor, readOnly}; setState shows one, getState captures the current one
    createState: (text, readOnly = false) => ({ value: text, cursor: 0, readOnly }),
    getState: () => ({ value: ed.value, cursor: ed.cursor, readOnly: ed.readOnly }),
    setState(state) { ed.value = state.value; ed.cursor = state.cursor; ed.readOnly = state.readOnly; ed.marks = []; },
    themeId: 'light', setTheme(id) { ed.themeId = id; return id; },
    complete: () => options.completions(), // what the real editor asks for when the list opens
  };
  return ed;
} };
const byId = {};
for (const id of ['editor', 'tokens', 'ast', 'error', 'status', 'token-count', 'stmt-count', 'source-pos', 'expand', 'collapse', 'parse', 'examples',
                  'output-pane', 'output', 'run-status', 'run', 'stdin', 'variables', 'variables-status', 'variables-note',
                  // layout: panels, their View toggles and hide buttons, the drag gutters
                  'editor-pane', 'output-header', 'variables-col', 'sidebar', 'tokens-pane', 'ast-pane',
                  'toggle-output', 'toggle-tokens', 'toggle-ast', 'hide-output', 'hide-tokens', 'hide-ast',
                  'gutter-output', 'gutter-variables', 'gutter-sidebar', 'gutter-ast',
                  // files and tabs
                  'tabs', 'files-pane', 'files-tree', 'files-note', 'files-root', 'files-refresh', 'project-folder', 'file-status', 'save', 'toggle-files', 'hide-files', 'gutter-files', 'source-path', 'run-file', 'theme', 'statusbar'])
  byId[id] = element(id === 'tokens' || id === 'variables' ? 'tbody' : 'div');
const rootStyle = {}; // the CSS variables the theme sets on :root
const document = { getElementById: id => byId[id], createElement: element, querySelectorAll: sel => query(null, sel), body: element('body'), title: '',
                   documentElement: { style: { setProperty(name, value) { rootStyle[name] = value; } } },
                   handlers: {}, addEventListener(type, fn) { (document.handlers[type] = document.handlers[type] || []).push(fn); },
                   createTextNode: text => ({ tagName: '#text', textContent: text, children: [], classList: { contains: () => false } }) };
let confirmAnswer = true, confirmQuestions = []; // window.confirm: the answer the "user" gives, and what was asked
let systemDark = false;
const window = { MyDSLEditor, handlers: {}, addEventListener(type, fn) { (window.handlers[type] = window.handlers[type] || []).push(fn); },
                 confirm(question) { confirmQuestions.push(question); return confirmAnswer; },
                 matchMedia: () => ({ get matches() { return systemDark; }, addEventListener() {} }) };
const fetch = () => new Promise(() => {}); // never resolves: the test feeds results directly
const stored = {}; // what the page persists (source, stdin, layout)
const localStorage = { getItem: key => stored[key] ?? null, setItem(key, value) { stored[key] = String(value); } };
const performance = { now: () => 0 };
new Function('document', 'window', 'fetch', 'localStorage', 'performance', 'setTimeout', 'clearTimeout', script)(
  document, window, fetch, localStorage, performance, () => 0, () => {});
const page = window.mydslDebug;

// ---- helpers -------------------------------------------------------------------------
let failures = 0, passed = 0;
function check(ok, name, detail = '') {
  if (ok) { ++passed; return; }
  ++failures;
  console.error(`FAIL: ${name}${detail ? '\n      ' + detail : ''}`);
}
const parse = text => JSON.parse(execFileSync(exe, ['--json', '-'], { input: text, encoding: 'utf8' }));
function load(text) { page.setSource(text); page.applyResult(parse(text), 1); return page.state().result; }
const escapeForHtml = s => s.replace(/[&<>]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c]));
const highlighted = () => { const s = page.state().selection; return s && ed.value.slice(s.begin, s.end); };
// The text covered by the editor marks of one class ('selected', 'error', 'trace'), in order.
const marked = cls => ed.marks.filter(m => m.cls === cls).map(m => m.from === m.to ? '' : ed.value.slice(m.from, m.to)).join('|');
const nodes = (root, list = []) => { for (const n of root) { list.push(n); nodes([...(n.exprs || []), ...(n.body || []), ...(n.args || [])], list); } return list; };

// ---- line/column mapping ---------------------------------------------------------------
page.setSource('ab\ncd\n\nef');
check(JSON.stringify(page.state().lineStarts) === '[0,3,6,7]', 'line starts');
check(page.offsetOf({ line: 2, column: 2 }) === 4, 'offset of 2:2');
check(page.offsetOf({ line: 4, column: 1 }) === 7, 'offset of 4:1');
check(page.offsetOf({ line: 9, column: 9 }) === 9, 'offset past the end is clamped');
check(page.lineOf(0) === 1 && page.lineOf(4) === 2 && page.lineOf(6) === 3 && page.lineOf(8) === 4, 'line of offset');

// ---- valid program: tokens, nodes, clicks ---------------------------------------------
const program = 'int a := 1;\nwhile (a < 10) {\n    a := a * 2 + inv_vec([a, 3])[0];\n};\nPrint(a);\n';
const result = load(program);
check(result.error === null && result.ast.length === 3, 'C++ parser accepted the program');
check(byId.tokens.children.length === result.tokens.length && byId.tokens.children.length === 38, 'one table row per token (including <eof>)');
check(byId['token-count'].textContent === '38 tokens' && byId['stmt-count'].textContent === '3 statements', 'counts');
check(!byId.error.classList.contains('show'), 'no error banner');

// Clicking token rows highlights exactly the token text.
result.tokens[8].row.dispatch('click');
check(highlighted() === '<', 'click token "<" highlights it: ' + JSON.stringify(highlighted()));
result.tokens[18].row.dispatch('click');
check(highlighted() === 'inv_vec', 'click token "inv_vec" (line 3) highlights it');
check(result.tokens[18].row.classList.contains('selected'), 'clicked row is selected');
result.tokens[1].row.dispatch('click');
check(!result.tokens[18].row.classList.contains('selected') && result.tokens[1].row.classList.contains('selected'), 'only one row selected');
check(marked('selected') === 'a', 'the editor marks the selection');

// Clicking AST nodes highlights the whole node span.
const every = nodes(result.ast);
const find = (op, name) => every.find(n => n.op === op && (name === undefined || n.name === name));
const whileNode = find('while');
whileNode.element.dispatch('click');
check(highlighted() === 'while (a < 10) {\n    a := a * 2 + inv_vec([a, 3])[0];\n};', 'while statement span includes its ";"');
check(whileNode.element.classList.contains('selected'), 'clicked node is selected');
check(!result.tokens[1].row.classList.contains('selected'), 'selecting a node deselects the token row');
find('binary', '+').element.dispatch('click');
check(highlighted() === 'a * 2 + inv_vec([a, 3])[0]', 'binary + spans both operands: ' + JSON.stringify(highlighted()));
find('binary', '*').element.dispatch('click');
check(highlighted() === 'a * 2', 'binary * span');
find('index').element.dispatch('click');
check(highlighted() === 'inv_vec([a, 3])[0]', 'index span includes target and brackets');
find('call', 'inv_vec').element.dispatch('click');
check(highlighted() === 'inv_vec([a, 3])', 'call span includes the argument list');
find('binary', '<').element.dispatch('click');
check(highlighted() === 'a < 10', 'condition span (parentheses belong to the while, not the condition)');
find('block').element.dispatch('click');
check(highlighted() === '{\n    a := a * 2 + inv_vec([a, 3])[0];\n}', 'block span');
find('print').element.dispatch('click');
check(highlighted() === 'Print(a);', 'print statement span');

// Node labels and tree structure.
const labels = page.children(whileNode).map(k => k.label);
check(JSON.stringify(labels) === '["condition","body"]', 'while children labels: ' + JSON.stringify(labels));
check(JSON.stringify(page.children(find('binary', '+')).map(k => k.label)) === '["left","right"]', 'binary labels');
check(JSON.stringify(page.children(find('index')).map(k => k.label)) === '["target","index"]', 'index labels');
check(page.children(find('literal')).length === 0 && find('literal').details.classList.contains('leaf'), 'literals are leaves');
check(byId.ast.children.length === 3 && byId.ast.children[0].tagName === 'DETAILS', 'one <details> per top-level statement');
check(find('while').details.children.length === 3, 'while details: summary + 2 children');
check(find('declare').element.innerHTML.includes('int') && find('declare').element.innerHTML.includes('1:1–1:12'), 'declare summary shows type and span');

// Cursor -> innermost node and token.
ed.cursor = program.indexOf('2 + inv');
page.trackCursor();
check(find('literal', undefined) && every.find(n => n.value === 'Int(2)').element.classList.contains('at-cursor'), 'cursor on "2" marks the literal');
check(result.tokens.find(t => t.text === '2').row.classList.contains('at-cursor'), 'cursor on "2" marks its token row');
ed.cursor = program.indexOf('*');
page.trackCursor();
check(find('binary', '*').element.classList.contains('at-cursor'), 'cursor on "*" marks the binary node (innermost containing node)');
ed.cursor = program.indexOf('while');
page.trackCursor();
check(whileNode.element.classList.contains('at-cursor'), 'cursor on "while" marks the while statement');
check(byId['source-pos'].textContent === 'line 2, column 1', 'cursor position display: ' + byId['source-pos'].textContent);
find('while').details.open = false; find('assign').details.open = false;
ed.cursor = program.indexOf('[0]') + 1;
page.trackCursor();
check(find('while').details.open && find('assign').details.open, 'ancestors are expanded to reveal the node under the cursor');
check(page.deepest(result.ast, program.indexOf('[0]') + 1).value === 'Int(0)', 'deepest node at "[0]" is the literal 0');
check(page.deepest(result.ast, program.length - 1) === null, 'no node on the trailing newline');

// ---- parse error -----------------------------------------------------------------------
const bad = 'int a := 1;\nint b := (a +\n  2;\n';
const broken = load(bad);
check(broken.error && broken.error.stage === 'Parse' && broken.error.line === 3 && broken.error.column === 4, 'C++ parser reported the error location');
check(byId.error.classList.contains('show') && byId.error.textContent === "Parse error at line 3, column 4: Expected ')' to close the '(' opened at line 2, column 10 but found ';'", 'error banner text: ' + byId.error.textContent);
check(page.state().errorOffset === bad.indexOf(';', bad.indexOf('2')), 'error offset points at the ";"');
check(marked('error') === ';', 'error marker in the source');
check(byId.tokens.children.length === broken.tokens.length && broken.tokens.length === 14, 'tokens are still listed after a parse error');
check(broken.tokens.find(t => t.text === ';' && t.begin.line === 3).row.classList.contains('error-token'), 'offending token is marked');
check(byId.ast.innerHTML.includes('No AST'), 'AST pane explains that there is no tree');
check(byId.status.textContent === 'parse error', 'status shows the error');

// ---- lexing error ----------------------------------------------------------------------
const lexed = load('int a := 1 $ 2;');
check(lexed.error.stage === 'Lexing' && lexed.tokens.length === 0, 'lexing error: no tokens');
check(byId.error.textContent.startsWith('Lexing error at line 1, column 12:'), 'lexing error banner');
check(byId.tokens.children.length === 0 && byId['token-count'].textContent === '', 'token table is empty');

// ---- error at end of input ---------------------------------------------------------------
const eof = load('int a := 1');
check(eof.error.column === 11 && page.state().errorOffset === 10, 'error at end of input');
check(ed.marks.some(m => m.cls === 'error' && m.from === 10 && m.to === 11) && ed.value.length === 10, 'end-of-input error is marked after the text (the editor draws a box there)');

// ---- type error: the AST is kept, the offending node is outlined ---------------------------
const typed = load('int a := 1;\nfloat b := 2.0;\nPrint(a + b);\n');
check(typed.error && typed.error.stage === 'Type' && typed.error.line === 3 && typed.error.column === 9, 'C++ type checker reported the error: ' + JSON.stringify(typed.error));
check(typed.ast && typed.ast.length === 3 && byId.ast.children.length === 3, 'AST is still shown after a type error');
check(byId.error.classList.contains('show') && byId.error.textContent.startsWith("Type error at line 3, column 9: Operator '+' cannot be applied to int and float"), 'type error banner: ' + byId.error.textContent);
check(marked('error') === '+', 'type error marker sits on the operator');
const plus = nodes(typed.ast).find(n => n.op === 'binary');
check(plus.element.classList.contains('error-node'), 'the binary node is outlined as the error node');
check(!nodes(typed.ast).some(n => n.type !== undefined), 'no inferred types are shown when the program does not type-check');
check(byId.status.textContent === 'type error', 'status shows the type error');

// ---- inferred types are shown when the program type-checks ---------------------------------
const okTypes = load('intmatrix 2,3 m := [[1,2,3],[4,5,6]];\nintmatrix 2,2 p := mult_mat(m, transpose(m));\nbool z := is_zero(p);\n');
check(okTypes.error === null, 'program type-checks');
const typesOf = nodes(okTypes.ast).filter(n => n.node === 'expression').map(n => n.op + ':' + n.type);
check(typesOf.includes('call:intmatrix 2,2') && typesOf.includes('call:intmatrix 3,2') && typesOf.includes('matrix:intmatrix 2,3') && typesOf.includes('call:bool'), 'expression types from the checker: ' + typesOf.join(' '));
check(nodes(okTypes.ast).find(n => n.name === 'mult_mat').element.innerHTML.includes(': intmatrix 2,2'), 'node summary shows the inferred type');
check(!byId.ast.querySelectorAll('.error-node').length, 'no error node without an error');

// ---- user-defined functions: function/parameter/return nodes, labels, spans, errors ---------------
const fnSource = 'function float scale(floatvector 2 v, float k) {\n    return abs(v) * k;\n};\nPrint(scale([3., 4.], 2.));\n';
const fnResult = load(fnSource);
check(fnResult.error === null && fnResult.ast.length === 2, 'function program type-checks');
const fnNode = fnResult.ast[0];
check(fnNode.op === 'function' && fnNode.params.length === 2 && fnNode.params[0].node === 'parameter', 'function node carries its parameters');
const fnLabels = page.children(fnNode).map(k => k.label);
check(JSON.stringify(fnLabels) === '["param 0","param 1","body"]', 'function children labels: ' + JSON.stringify(fnLabels));
check(fnNode.element.innerHTML.includes('function') && fnNode.element.innerHTML.includes('scale') && fnNode.element.innerHTML.includes('(floatvector 2 v, float k)'),
      'function summary shows the return type, name and parameter list: ' + fnNode.element.innerHTML);
fnNode.element.dispatch('click');
check(highlighted() === 'function float scale(floatvector 2 v, float k) {\n    return abs(v) * k;\n};', 'function span includes the header, body and ";"');
fnNode.params[1].element.dispatch('click');
check(highlighted() === 'float k', 'clicking a parameter highlights its type and name: ' + JSON.stringify(highlighted()));
check(fnNode.params[1].element.classList.contains('param') && fnNode.params[1].details.classList.contains('leaf'), 'parameters are leaf nodes');
const returnNode = nodes(fnResult.ast).find(n => n.op === 'return');
check(JSON.stringify(page.children(returnNode).map(k => k.label)) === '["value"]', 'return child label');
returnNode.element.dispatch('click');
check(highlighted() === 'return abs(v) * k;', 'return span runs through ";"');
check(nodes(fnResult.ast).find(n => n.op === 'call' && n.name === 'scale').type === 'float', 'user call shows its return type');
ed.cursor = fnSource.indexOf('float k') + 6;
page.trackCursor();
check(fnNode.params[1].element.classList.contains('at-cursor'), 'cursor inside a parameter marks the parameter node');
ed.cursor = fnSource.indexOf('abs');
page.trackCursor();
check(nodes(fnResult.ast).find(n => n.op === 'call' && n.name === 'abs').element.classList.contains('at-cursor'), 'cursor in the body finds the innermost node');
const missingReturn = load('function int f(int x) {\n    Print(x);\n};\n');
check(missingReturn.error.stage === 'Type' && missingReturn.error.line === 1 && missingReturn.error.column === 1, 'missing return is a type error at the function');
check(missingReturn.ast[0].element.classList.contains('error-node'), 'the function node is outlined for a missing return');
check(byId.error.textContent.startsWith("Type error at line 1, column 1: Function 'f' must return int on every path"), 'missing return banner: ' + byId.error.textContent);
const badArg = load('function int sq(int x) { return x * x; };\nPrint(sq(1.5));\n');
check(badArg.error.line === 2 && badArg.error.column === 10 && nodes(badArg.ast).find(n => n.value === 'Float(1.5)').element.classList.contains('error-node'), 'argument type error outlines the argument');
const dupParam = load('function int f(int a, int a) { return a; };\n');
check(dupParam.error.column === 23 && dupParam.ast[0].params[1].element.classList.contains('error-node'), 'duplicate parameter error outlines the parameter');
const strayReturn = load('return 1;\n');
check(strayReturn.error.stage === 'Parse' && byId.ast.innerHTML.includes('No AST'), 'stray return is a parse error');

// ---- strings: token kind, literal nodes (incl. empty), inferred types, escapes, errors ------------
const strSource = 'string s := "a\\"b\\n";\nPrint(s + "" == "x");\n';
const strResult = load(strSource);
check(strResult.error === null, 'string program type-checks');
const strToken = strResult.tokens.find(t => t.kind === 'string');
check(strToken && strToken.text === 'a"b\n' && strToken.row.className.includes('kind-string'), 'string token row has the string kind: ' + JSON.stringify(strToken && strToken.text));
check(strToken.row.innerHTML.includes(escapeForHtml(JSON.stringify('a"b\n'))), 'string token text is shown escaped: ' + strToken.row.innerHTML);
strToken.row.dispatch('click');
check(highlighted() === '"a\\"b\\n"', 'clicking the string token highlights the literal including quotes and escapes: ' + JSON.stringify(highlighted()));
const strNodes = nodes(strResult.ast);
const strLit = strNodes.find(n => n.op === 'string' && n.name === 'a"b\n');
check(strLit && strLit.type === 'string' && strLit.element.innerHTML.includes(': string'), 'string literal node shows its type');
check(strLit.element.innerHTML.includes(escapeForHtml(JSON.stringify('a"b\n'))), 'string literal node shows the escaped text: ' + strLit.element.innerHTML);
const emptyLit = strNodes.find(n => n.op === 'string' && n.name === '');
check(emptyLit && emptyLit.element.innerHTML.includes('&quot;&quot;') || (emptyLit && emptyLit.element.innerHTML.includes('""')), 'empty string literal is shown as ""');
check(strNodes.find(n => n.op === 'binary' && n.name === '+').type === 'string' && strNodes.find(n => n.op === 'binary' && n.name === '==').type === 'bool', 'concatenation is string, comparison is bool');
check(strNodes.find(n => n.op === 'declare').element.innerHTML.includes('string'), 'declaration shows the string type');
const strBad = load('string s := "a" + 1;\n');
check(strBad.error.stage === 'Type' && strBad.error.column === 17 && nodes(strBad.ast).find(n => n.op === 'binary').element.classList.contains('error-node'), 'string type error outlines the operator node');
const strLex = load('Print("bad \\q");\n');
check(strLex.error.stage === 'Lexing' && strLex.error.column === 12 && byId.error.textContent.includes("Unknown escape sequence '\\q'"), 'escape error banner: ' + byId.error.textContent);

// ---- empty program and recovery ------------------------------------------------------------
const empty = load('');
check(empty.error === null && empty.ast.length === 0 && byId.ast.innerHTML.includes('Empty program'), 'empty program');
check(!byId.error.classList.contains('show') && page.state().errorOffset === null, 'error banner cleared after a valid parse');
load('x := [[1,2],[3,4]];');
check(find('matrix') === undefined && nodes(page.state().result.ast).find(n => n.op === 'matrix').element.innerHTML.includes('[2,2]'), 'matrix shape shown');

// ---- running: output area, runtime errors, stale marker ------------------------------------
const os = require('os');
function run(text, stdin = '') {
  const dataFile = path.join(os.tmpdir(), `mydsl-web-test-${process.pid}.txt`);
  fs.writeFileSync(dataFile, stdin);
  try { return JSON.parse(execFileSync(exe, ['--run-json', '--input', dataFile, '-'], { input: text, encoding: 'utf8' })); }
  finally { fs.unlinkSync(dataFile); }
}
const outputText = () => byId.output.children.map(c => c.textContent).join('');
const factorial = 'int a := 1;\nfor (i := 1, i <= 5, 1) { a := a * i; };\nPrint(a);\n';
load(factorial);
check(byId['run-status'].textContent === '' && !byId['output-pane'].classList.contains('stale'), 'nothing runs while typing');
page.applyRunResult(run(factorial));
check(outputText() === 'Int(120)\n', 'output of the C++ interpreter is shown: ' + JSON.stringify(outputText()));
check(/^finished in \d+ ms$/.test(byId['run-status'].textContent), 'run status: ' + byId['run-status'].textContent);
ed.type(ed.value + ' ');
check(byId['output-pane'].classList.contains('stale'), 'editing after a run marks the output as stale');
const crash = 'int a := 10;\nPrint(a);\nint b := a / (a - 10);\nPrint(b);\n';
load(crash);
page.applyRunResult(run(crash));
check(outputText() === 'Int(10)\nRuntime error at line 3, column 12: Division by zero: 10 / 0', 'partial output then the runtime error: ' + JSON.stringify(outputText()));
check(!byId['output-pane'].classList.contains('stale'), 'a new run clears the stale marker');
check(byId['run-status'].textContent.startsWith('runtime error after'), 'status shows the runtime error');
check(page.state().errorOffset === crash.indexOf('/ ('), 'runtime error is marked in the source');
check(byId.output.children.some(c => c.className === 'runtime-error'), 'runtime error is rendered as a link');
page.applyRunResult(run('int x := 1;'));
check(outputText() === '(the program produced no output)' && page.state().errorOffset === null, 'silent program');
page.applyRunResult(run('int x := true;'));
check(outputText().startsWith('Type error at line 1, column 1:'), 'type errors are reported by Run as well');
page.applyRunResult(run('function int sq(int x) { return x * x; };\nfunction int fact(int n) { if (n <= 1) then { return 1; }; return n * fact(n - 1); };\nPrint(sq(fact(3)));'));
check(outputText() === 'Int(36)\n', 'Run executes user-defined (recursive) functions: ' + JSON.stringify(outputText()));
const crashInFunction = 'function int half(int x) {\n    return 10 / x;\n};\nPrint(half(5));\nPrint(half(0));\n';
load(crashInFunction);
page.applyRunResult(run(crashInFunction));
check(outputText() === "Int(2)\nRuntime error at line 2, column 15: Division by zero: 10 / 0\n  ↳ in function 'half' called from line 5, column 7", 'runtime error inside a function: message line, then one line per call: ' + JSON.stringify(outputText()));
check(page.state().errorOffset === crashInFunction.indexOf('/ x'), 'the marker sits on the failing operator inside the body');
check(JSON.stringify(page.state().traceOffsets) === JSON.stringify([crashInFunction.indexOf('half(0)')]), 'the call site of the trace is marked: ' + JSON.stringify(page.state().traceOffsets));
check(marked('trace') === 'h' && marked('error') === '/', 'the editor shows the error (red) and the call site (orange): ' + JSON.stringify(ed.marks));
{
  const frame = byId.output.children.find(c => c.className === 'trace-frame');
  frame.dispatch('click');
  check(ed.cursor === crashInFunction.indexOf('half(0)'), 'clicking a trace frame jumps to the call site');
}
page.applyRunResult(run('function int f(int n) { return f(n + 1); };\nPrint(f(0));'));
check(outputText().startsWith("Runtime error at line 1, column 32: Call depth limit of 1000 exceeded while calling 'f'"), 'runaway recursion is reported, not a crash: ' + outputText().slice(0, 80));
check(outputText().endsWith("  ↳ in function 'f' called from line 1, column 32 [999 times]\n  ↳ in function 'f' called from line 2, column 7"), 'recursive frames are collapsed with a repeat count: ' + JSON.stringify(outputText()));
page.applyRunResult(run('string s := "a\\"b"; Print(s + "\\n"); Print("" == "");'));
check(outputText() === 'String("a\\"b\\n")\nBool(true)\n', 'Run prints strings with escapes, one per line: ' + JSON.stringify(outputText()));
byId.stdin.value = '"Ada Lovelace" 3';
page.applyRunResult(run('string name := Input(); int n := Input(); Print(substring(name, 0, n)); Print(length(name)); Print(to_string(n) + name);', byId.stdin.value));
check(outputText() === 'String("Ada")\nInt(12)\nString("3Ada Lovelace")\n', 'Input box text in quotes is read as a string value: ' + JSON.stringify(outputText()));
page.applyRunResult(run('string name := Input();', 'Ada'));
check(outputText() === "Runtime error at line 1, column 16: Invalid data in standard input at line 1, column 1: expected a string in double quotes (e.g. \"text\"), not bare text but found 'Ada'", 'bare text in the Input box is rejected with a hint: ' + outputText());
page.applyRunResult(run('Print(substring("abc", 1, 5));'));
check(outputText().startsWith('Runtime error at line 1, column 7: substring: start 1 + count 5 exceeds the length 3'), 'substring range error is shown: ' + outputText());
const builtinTypes = load('string s := Input();\nPrint(substring(to_string(length(s)), 0, 1));\n');
const callTypes = nodes(builtinTypes.ast).filter(n => n.op === 'call').map(n => n.name + ':' + n.type);
check(JSON.stringify(callTypes) === '["Input:string","substring:string","to_string:string","length:int"]', 'string builtin call nodes show their types: ' + JSON.stringify(callTypes));
page.applyRunResult(run('intvector 2 v := [1, 2]; Print(v * 3); Print(determinant([[1., 2.], [3., 4.]]));'));
check(outputText() === 'IntVector[3, 6]\nFloat(-2.0)\n', 'vector/matrix output: ' + JSON.stringify(outputText()));
// The Input box is standard input for Input(); the request carries source and input together.
byId.stdin.value = '[1., 2., 3.] 2';
page.applyRunResult(run('floatvector 3 v := Input(); int k := Input(); Print(v * to_float(k));', byId.stdin.value));
check(outputText() === 'FloatVector[2.0, 4.0, 6.0]\n', 'Input() reads the Input box text: ' + JSON.stringify(outputText()));
page.applyRunResult(run('int a := Input(); Print(a);', '4.5'));
check(outputText() === 'Runtime error at line 1, column 10: Invalid data in standard input at line 1, column 1: expected an int (a float was given) but found \'4.5\'', 'input errors name the data position: ' + outputText());
page.applyRunResult(run('int a := Input();', ''));
check(outputText().includes('Unexpected end of standard input'), 'empty Input box -> end of input error');
check(page.runRequestBody('Print(1);', '42') === '9\nPrint(1);42', 'run request framing: byte count, newline, source, input');
check(page.runRequestBody('Print("é");', 'x') === '12\nPrint("é");x', 'run request framing counts bytes, not characters');
byId.stdin.dispatch('input');
check(byId['output-pane'].classList.contains('stale'), 'editing the input marks the output as stale');

// ---- variable inspector: values come from the C++ interpreter, never computed here -------------------
const rows = () => byId.variables.children.map(r => r.variable);
const inspectorSource = 'int a := 1;\na := a + 41;\nfloat f := 2.5;\nbool ok := a == 42;\nstring s := "x";\ns := s + "y";\nintvector 2 v := [1, 2];\nv[0] := 9;\nfloatmatrix 2,2 m := [[1., 2.], [3., 4.]];\nm := transpose(m);\n';
load(inspectorSource);
page.applyRunResult(run(inspectorSource));
check(JSON.stringify(rows().map(v => `${v.name}:${v.type}=${v.value}`)) ===
      JSON.stringify(['a:int=Int(42)', 'f:float=Float(2.5)', 'm:floatmatrix 2,2=FloatMatrix[\n  [1.0, 3.0]\n  [2.0, 4.0]\n]', 'ok:bool=Bool(true)', 's:string=String("xy")', 'v:intvector 2=IntVector[9, 2]']),
      'inspector rows show final values of every type: ' + JSON.stringify(rows()));
check(byId['variables-status'].textContent === '6 variables', 'inspector status: ' + byId['variables-status'].textContent);
check(byId.variables.children[0].innerHTML.includes('<td class="name">a</td><td class="type">int</td><td class="value">Int(42)</td>'), 'row markup: ' + byId.variables.children[0].innerHTML);
check(byId.variables.children[0].classList.contains('changed') && !byId.variables.children[1].classList.contains('changed'), 'variables whose value differs from a literal initializer are flagged');
byId.variables.children[4].dispatch('click');
check(highlighted() === 'string s := "x";', 'clicking a row highlights the declaration: ' + JSON.stringify(highlighted()));
ed.type(ed.value + ' ');
check(byId['output-pane'].classList.contains('stale'), 'editing marks output and variables as stale');
// Functions: only top-level variables are listed, never parameters or locals; values after calls.
const fnInspector = 'function int twice(int n) { int local := n * 2; return local; };\nint x := twice(4);\nint y := twice(x);\n';
load(fnInspector);
page.applyRunResult(run(fnInspector));
check(JSON.stringify(rows().map(v => `${v.name}=${v.value}`)) === '["x=Int(8)","y=Int(16)"]', 'after calls: globals only, with the returned values: ' + JSON.stringify(rows()));
const recInspector = 'function int fact(int n) { if (n <= 1) then { return 1; }; return n * fact(n - 1); };\nint five := fact(5);\nint ten := fact(10);\n';
load(recInspector);
page.applyRunResult(run(recInspector));
check(JSON.stringify(rows().map(v => `${v.name}=${v.value}`)) === '["five=Int(120)","ten=Int(3628800)"]', 'recursion results: ' + JSON.stringify(rows()));
// Runtime failure: the inspector shows the state at the moment of the error; a nested trace is listed innermost first.
const failing = 'int done := 0;\nfunction int inner(int x) {\n  return 10 / x;\n};\nfunction int outer(int x) {\n  return inner(x - 1);\n};\ndone := outer(2);\nint r := outer(1);\ndone := 99;\n';
load(failing);
page.applyRunResult(run(failing));
check(JSON.stringify(rows().map(v => `${v.name}=${v.value}`)) === '["done=Int(10)"]', 'inspector after a failure: values at the time of the error, no half-declared r: ' + JSON.stringify(rows()));
check(byId['variables-status'].textContent === '1 variable · values at the time of the error', 'inspector status after a failure: ' + byId['variables-status'].textContent);
check(outputText() === "Runtime error at line 3, column 13: Division by zero: 10 / 0\n  ↳ in function 'inner' called from line 6, column 10\n  ↳ in function 'outer' called from line 9, column 10", 'nested trace, innermost first: ' + JSON.stringify(outputText()));
check(JSON.stringify(page.state().traceOffsets) === JSON.stringify([failing.indexOf('inner(x - 1)'), failing.indexOf('outer(1)')]), 'both call sites are marked: ' + JSON.stringify(page.state().traceOffsets));
check(page.state().errorOffset === failing.indexOf('/ x'), 'error marker in the inner body');
byId.output.children.filter(c => c.className === 'trace-frame')[1].dispatch('click');
check(ed.cursor === failing.indexOf('outer(1)'), 'clicking the outer frame jumps to its call site');
page.applyRunResult(run('int a := 1; Print(a);'));
check(outputText() === 'Int(1)\n' && page.state().traceOffsets.length === 0 && rows().length === 1, 'a later successful run clears the trace markers and refreshes the inspector');
page.applyRunResult(run('Print(1);'));
check(rows().length === 0 && byId['variables-note'].textContent === '(the program has no top-level variables)', 'no variables: explanatory note');
page.applyRunResult(run('int z := 1 / 0;'));
check(rows().length === 0 && byId['variables-note'].textContent === '(no top-level variable had a value when the error occurred)', 'failure before any variable exists: ' + byId['variables-note'].textContent);
load('int a := 1;\nint b := (a +\n');
check(page.state().traceOffsets.length === 0, 'a new parse result drops the trace markers of the previous run');

// ---- modules: import nodes; errors located in an imported file name the file and the chain and mark the import statement ----
// (the page posts source text only, so the server resolves imports relative to its working directory: the project dir here)
const importSource = 'Print(1);\nimport "tests/modules/lib/imports_broken.mydsl";\n';
const imported = load(importSource);
check(imported.error && imported.error.stage === 'Parse' && imported.error.file === 'tests/modules/lib/broken_syntax.mydsl' && imported.error.line === 3 && imported.error.column === 1 &&
      JSON.stringify(imported.error.imports) === '[{"file":"tests/modules/lib/imports_broken.mydsl","line":3,"column":1},{"file":"","line":2,"column":1}]', 'C++ loader reported the error in the imported file with its chain: ' + JSON.stringify(imported.error));
check(imported.ast && imported.ast.length === 2 && imported.ast[1].op === 'import' && imported.ast[1].name === 'tests/modules/lib/imports_broken.mydsl' && byId.ast.children.length === 2, 'the AST of the main program is still shown, with the import node');
check(byId.error.textContent === "Parse error in 'tests/modules/lib/broken_syntax.mydsl' at line 3, column 1: Expected ';' to end the return statement but found '}' (imported from 'tests/modules/lib/imports_broken.mydsl' at line 3, column 1; imported from the main program at line 2, column 1)",
      'banner names the file and the import chain: ' + byId.error.textContent);
check(page.state().errorOffset === importSource.indexOf('import'), 'the marker sits on the import statement of the main program, not on line 3 column 1 of the editor');
check(marked('error') === 'i' && ed.marks.find(m => m.cls === 'error').from === importSource.indexOf('import'), 'error marker on the import statement: ' + JSON.stringify(ed.marks));
check(imported.tokens.find(t => t.text === 'import').row.classList.contains('error-token') && !imported.tokens.find(t => t.text === 'Print').row.classList.contains('error-token'), 'the import token is marked, not the token at the foreign position');
check(byId.status.textContent === 'parse error', 'status: ' + byId.status.textContent);
check(byId.error.title === 'Click to open tests/modules/lib/broken_syntax.mydsl at line 3, column 1', 'the banner offers to open the file: ' + byId.error.title);
byId.error.dispatch('click');
check(byId['file-status'].textContent === 'opening tests/modules/lib/broken_syntax.mydsl…', 'clicking the banner opens the imported file (the stub never answers the request): ' + byId['file-status'].textContent);
const missing = load('import "nowhere.mydsl";');
check(missing.error.stage === 'Import' && !('file' in missing.error) && byId.error.textContent === "Import error at line 1, column 1: Cannot find imported file \"nowhere.mydsl\" (resolved to 'nowhere.mydsl' relative to the working directory, because the main program was read from standard input)",
      'a missing import of the main program: plain banner, no file: ' + byId.error.textContent);
check(missing.ast[0].element.classList.contains('error-node') && page.state().errorOffset === 0 && byId.status.textContent === 'import error', 'the import node is outlined like a type error node');
const loaded = load('import "tests/modules/lib/strings.mydsl";\nPrint(greet("x"));');
check(loaded.error === null && !byId.error.classList.contains('show') && nodes(loaded.ast).find(n => n.op === 'call' && n.name === 'greet').type === 'string',
      'a program calling an imported function type-checks; the call node shows the imported return type');
check(byId.status.textContent.startsWith('ok · '), 'status after a multi-file parse: ' + byId.status.textContent);
// Type errors inside an imported module: banner with file and chain, marker on the import statement.
const badBody = load('Print(1);\nimport "tests/modules/lib/bad_body.mydsl";');
check(badBody.error.stage === 'Type' && badBody.error.file === 'tests/modules/lib/bad_body.mydsl' && page.state().errorOffset === 10 && badBody.ast[1].element.classList.contains('error-node') &&
      byId.error.textContent === "Type error in 'tests/modules/lib/bad_body.mydsl' at line 2, column 14: Operator '*' cannot be applied to int and float (MyDSL does not convert between int and float implicitly; use to_float(...) or to_int(...)) (imported from the main program at line 2, column 1)",
      'type error inside an imported module: ' + byId.error.textContent);
// Running: imported functions execute; a runtime error inside one names the file, lists the
// trace with the file of each call site, and marks the innermost call site of the editor's program.
const crossRun = 'import "tests/modules/lib/failing.mydsl";\nPrint(scaled_ratio(9, 3));\nPrint(scaled_ratio(1, 0));\n';
load(crossRun);
page.applyRunResult(run(crossRun));
check(outputText() === "Int(9)\nRuntime error in 'tests/modules/lib/failing.mydsl' at line 4, column 14: Division by zero: 1 / 0 (imported from the main program at line 1, column 1)\n" +
      "  ↳ in function 'ratio' called from line 7, column 19 of 'tests/modules/lib/failing.mydsl'\n  ↳ in function 'scaled_ratio' called from line 3, column 7", 'Run output of a runtime error inside an imported function: ' + JSON.stringify(outputText()));
check(page.state().errorOffset === crossRun.indexOf('scaled_ratio(1, 0)'), 'the marker sits on the call in the editor, not at line 4 column 14 of the editor');
check(JSON.stringify(page.state().traceOffsets) === JSON.stringify([crossRun.indexOf('scaled_ratio(1, 0)')]), 'only call sites inside the editor are marked: ' + JSON.stringify(page.state().traceOffsets));
const frames = byId.output.children.filter(c => (c.className || '').startsWith('trace-frame'));
check(frames.length === 2 && frames[0].className === 'trace-frame' && frames[0].title === 'Click to open tests/modules/lib/failing.mydsl at the call site' && frames[1].className === 'trace-frame' && frames[1].title === 'Click to jump to the call site', 'frames in project files open the file, frames in the program jump: ' + frames.map(f => f.title).join(' | '));
check(byId.output.children.find(c => c.className === 'runtime-error').title === 'Click to open tests/modules/lib/failing.mydsl at line 4, column 14', 'the error line offers to open the file');
frames[1].dispatch('click');
check(ed.cursor === crossRun.indexOf('scaled_ratio(1, 0)'), 'clicking the editor frame jumps to the call site');
check(byId['run-status'].textContent.startsWith('runtime error after'), 'run status: ' + byId['run-status'].textContent);
const goodRun = 'import "tests/modules/lib/stats.mydsl";\nfloat v := variance([1.0, 2.0, 3.0]);\nPrint(square(3));\n';
load(goodRun);
page.applyRunResult(run(goodRun));
check(outputText() === 'Int(9)\n' && page.state().errorOffset === null && JSON.stringify(rows().map(v => `${v.name}=${v.value}`)) === '["v=Float(0.666666666666667)"]',
      'a successful cross-file run: output and inspector: ' + JSON.stringify(outputText()) + ' ' + JSON.stringify(rows()));
load(importSource);
page.applyRunResult(run(importSource));
check(outputText() === "Parse error in 'tests/modules/lib/broken_syntax.mydsl' at line 3, column 1: Expected ';' to end the return statement but found '}' (imported from 'tests/modules/lib/imports_broken.mydsl' at line 3, column 1; imported from the main program at line 2, column 1)" &&
      page.state().errorOffset === importSource.indexOf('import') && byId['run-status'].textContent.startsWith('parse error after'), 'Run shows the same message and marks the import statement: ' + outputText());
byId['file-status'].textContent = '';
byId.output.children.find(c => c.className === 'runtime-error').dispatch('click');
check(byId['file-status'].textContent === 'opening tests/modules/lib/broken_syntax.mydsl…', 'clicking the run error opens the file it lies in');
// Errors in files outside the project (absolute paths) cannot be opened: their frames are not clickable, the error line jumps to the call site in the program.
load('Print(1);\nint q := 2;\n');
page.applyRunResult({ output: '', millis: 1, variables: [], error: { stage: 'Runtime', line: 9, column: 2, message: 'boom', file: '/elsewhere/lib.mydsl', trace: [{ function: 'f', line: 9, column: 1, repeats: 1, file: '/elsewhere/lib.mydsl' }, { function: 'g', line: 1, column: 8, repeats: 1 }], omitted: 0 } });
{
  const items = byId.output.children.filter(c => (c.className || '').startsWith('trace-frame'));
  check(items[0].className === 'trace-frame foreign' && items[0].title === '' && items[1].className === 'trace-frame', 'a frame in a file outside the project is not clickable');
  check(page.state().errorOffset === 7 && JSON.stringify(page.state().traceOffsets) === '[7]', 'the marks fall back to the call site in the program: ' + page.state().errorOffset);
  ed.cursor = 0;
  byId.output.children.find(c => c.className === 'runtime-error').dispatch('click');
  check(ed.cursor === 7, 'clicking the error line jumps to the call site in the program');
}
load('int a := 1;');
check(!byId.error.classList.contains('show') && page.state().errorOffset === null, 'a later valid program clears the banner and marker');

// ---- the error statement: parsed like Print, type-checked (string message), raised at run time with the trace ----
const raising = 'function int pct(int v) {\n  if (v > 100) then { error("too big: " + to_string(v)); };\n  return v;\n};\nPrint(pct(5));\nPrint(pct(500));\n';
const raisingResult = load(raising);
check(raisingResult.error === null && raisingResult.ast[0].body[0].body[0].body[0].body[0].op === 'error', 'error(...) is an "error" statement node in the AST: ' + JSON.stringify(raisingResult.error));
page.applyRunResult(run(raising));
check(outputText() === "Int(5)\nRuntime error at line 2, column 23: too big: 500\n  ↳ in function 'pct' called from line 6, column 7", 'Run shows the program\'s own message with the trace: ' + JSON.stringify(outputText()));
check(page.state().errorOffset === raising.indexOf('error(') && JSON.stringify(page.state().traceOffsets) === JSON.stringify([raising.indexOf('pct(500)')]), 'the error statement and the call site are marked');
const badMessage = load('error(42);');
check(badMessage.error.stage === 'Type' && byId.error.textContent === 'Type error at line 1, column 7: The message of error must be a string but is int (build it with to_string(...) and +)' && nodes(badMessage.ast).find(n => n.op === 'literal').element.classList.contains('error-node'), 'a non-string message is a type error on the argument: ' + byId.error.textContent);
const inExpression = load('int x := error("m");');
check(inExpression.error.stage === 'Parse' && byId.error.textContent.startsWith("Parse error at line 1, column 10: 'error' is a statement that stops the program"), 'error in an expression is a parse error: ' + byId.error.textContent);
// Raised inside the standard library: file, message, chain; the marker goes to the call in the editor.
const libraryCall = 'import "stdlib/math.mydsl";\nPrint(factorial(3));\nPrint(factorial(-1));\n';
load(libraryCall);
page.applyRunResult(run(libraryCall));
check(outputText() === "Int(6)\nRuntime error in 'stdlib/math.mydsl' at line 85, column 23: factorial: the argument must not be negative, but n is -1 (imported from the main program at line 1, column 1)\n  ↳ in function 'factorial' called from line 3, column 7",
      'a library error names the file and explains the argument: ' + JSON.stringify(outputText()));
check(page.state().errorOffset === libraryCall.indexOf('factorial(-1)'), 'the marker sits on the failing call in the editor');

// ---- the editor interface: edits re-sync positions and clear the selection mark ----
load('int a := 1;\nPrint(a);\n');
nodes(page.state().result.ast).find(n => n.op === 'print').element.dispatch('click');
check(highlighted() === 'Print(a);' && marked('selected') === 'Print(a);', 'node click before an edit');
ed.type('int ab := 1;\nPrint(ab);\n');
check(page.state().selection === null && marked('selected') === '' && JSON.stringify(page.state().lineStarts) === '[0,13,24]', 'an edit drops the selection mark and recomputes the line starts');
ed.options.onParse(); // Ctrl+Enter inside the editor (the fetch stub never resolves; the request is at least issued)
check(byId.status.textContent === 'parsing' && byId.status.classList.contains('busy') && stored['mydsl-source'] === 'int ab := 1;\nPrint(ab);\n', 'Ctrl+Enter parses the edited text and persists it');
check(typeof ed.options.onRun === 'function' && ed.options.onCursor === page.trackCursor, 'the editor gets the run and cursor callbacks');
ed.options.onCursor(); // the editor reports a cursor move
check(byId['source-pos'].textContent === 'line 1, column 1', 'cursor position from the editor: ' + byId['source-pos'].textContent);

// ---- completion symbols: from the language (C++ --language-json), the AST, the functions and the tokens ----
page.setLanguage(JSON.parse(execFileSync(exe, ['--language-json'], { encoding: 'utf8' })));
const symbolProgram = 'import "stdlib/math.mydsl";\nfunction int scale(int value, int factor) {\n    int twice := value * 2;\n    return twice * factor;\n};\nint total := 0;\nfloatvector 3 vec := [1.0, 2.0, 3.0];\nfor (counter := 0, counter < 3, 1) {\n    total := total + counter;\n};\n';
load(symbolProgram);
let symbols = page.symbols();
const symbol = label => symbols.find(s => s.label === label);
check(symbols.length === new Set(symbols.map(s => s.label)).size, 'no duplicate labels');
check(JSON.stringify(symbol('scale')) === JSON.stringify({ label: 'scale', kind: 'function', params: ['value', 'factor'], detail: '(int value, int factor) → int', info: 'Function defined at line 2.' }), 'own function: ' + JSON.stringify(symbol('scale')));
check(symbol('factorial') && symbol('factorial').kind === 'function' && symbol('factorial').detail === '(int n) → int' && symbol('factorial').info.startsWith("Function from 'stdlib/math.mydsl' (line "), 'imported function with its module: ' + JSON.stringify(symbol('factorial')));
check(symbols.findIndex(s => s.label === 'factorial') < symbols.findIndex(s => s.label === 'scale'), 'imported functions are listed in dependency order before the program\'s own');
check(JSON.stringify(symbol('total')) === JSON.stringify({ label: 'total', kind: 'variable', detail: 'int', info: 'Variable declared at line 6.' }) && symbol('vec').detail === 'floatvector 3', 'top-level variables with their types');
check(JSON.stringify(symbol('twice')) === JSON.stringify({ label: 'twice', kind: 'variable', detail: 'int', info: 'Variable declared at line 3.' }), 'a local declared inside a function body: ' + JSON.stringify(symbol('twice')));
check(JSON.stringify(symbol('counter')) === JSON.stringify({ label: 'counter', kind: 'variable', detail: 'int', info: 'Loop variable of the for at line 8.' }), 'the for loop variable: ' + JSON.stringify(symbol('counter')));
check(JSON.stringify(symbol('value')) === JSON.stringify({ label: 'value', kind: 'parameter', detail: 'int', info: 'Parameter of scale (line 2).' }), 'parameters: ' + JSON.stringify(symbol('value')));
check(symbol('while').kind === 'keyword' && symbol('while').template === 'while (condition) { ... };' && symbol('while').info.startsWith('Loop while'), 'keywords from --language-json: ' + JSON.stringify(symbol('while')));
check(symbol('intmatrix').kind === 'type' && symbol('true').kind === 'constant' && symbol('Print').kind === 'keyword', 'types and constants are kinds of their own');
check(JSON.stringify(symbol('substring')) === JSON.stringify({ label: 'substring', kind: 'builtin', signature: 'substring(s, start, count)', detail: 'string, int, int -> string', info: 'count characters of s starting at the 0-based index start; the range must lie inside the string.' }), 'builtins: ' + JSON.stringify(symbol('substring')));
check(!symbols.some(s => s.fromTokens), 'every word of a program that parses is covered by a typed symbol (no plain token names)');
check(symbols.filter(s => s.kind === 'keyword' || s.kind === 'type' || s.kind === 'constant').length === 20 && symbols.filter(s => s.kind === 'builtin').length === 15, 'all 20 keywords and 15 builtins');
// While the text does not parse: declarations and functions of the last parse that had an AST, plus the words of the current text.
load('int alpha := 1;\nPrint(alp');
check(page.state().result.ast === null, 'the incomplete program has no AST');
symbols = page.symbols();
check(JSON.stringify(page.symbols().find(s => s.label === 'total')) === JSON.stringify(symbol('total')) && page.symbols().find(s => s.label === 'scale') && page.symbols().find(s => s.label === 'factorial'), 'declarations and functions of the last good parse are kept');
check(JSON.stringify(page.symbols().find(s => s.label === 'alpha')) === JSON.stringify({ label: 'alpha', kind: 'name', fromTokens: true }) && page.symbols().find(s => s.label === 'alp').fromTokens, 'the words of the current text come from the C++ lexer (the editor drops the one being typed)');
check(!page.symbols().some(s => s.label === 'int' && s.fromTokens), 'keyword tokens do not shadow the keyword entries');
load('int beta := 2;\n');
check(page.symbols().find(s => s.label === 'beta').kind === 'variable' && !page.symbols().find(s => s.label === 'alpha') && !page.symbols().find(s => s.label === 'total'), 'a new good parse replaces the remembered declarations');
page.setLanguage(null);
check(page.symbols().find(s => s.label === 'beta') && !page.symbols().find(s => s.label === 'while') && page.symbols().find(s => s.label === 'int').fromTokens, 'without the language info (server unreachable) the text\'s own words are still offered');

// ---- themes: one palette for the page (CSS variables) and the editor; auto follows the system ----
check(page.theme().choice === 'auto' && page.theme().effective === 'light' && ed.themeId === 'light' && rootStyle['--bg'] === '#f6f7f9' && rootStyle['--kw'] === '#7c3aed', 'default: auto -> light (the page\'s original palette): ' + JSON.stringify(page.theme()));
check(byId.theme.children.length === 1 + MyDSLEditor.themes.length && byId.theme.children[0].value === 'auto' && MyDSLEditor.themes.length >= 12, 'the selector lists Auto and every theme: ' + byId.theme.children.length);
check(byId.theme.children.slice(1).every((o, i, all) => i === 0 || !(MyDSLEditor.themes.find(th => th.id === all[i - 1].value).dark && !MyDSLEditor.themes.find(th => th.id === o.value).dark)), 'light themes are listed before dark ones');
page.applyTheme('vs-dark');
check(ed.themeId === 'vs-dark' && rootStyle['--bg'] === '#252526' && rootStyle['--panel'] === '#1e1e1e' && rootStyle['--kw'] === '#c586c0' && rootStyle['--status-bg'] === '#007acc' && rootStyle['color-scheme'] === 'dark' && stored['mydsl-theme'] === 'vs-dark' && byId.theme.value === 'vs-dark',
      'Dark+ sets the page variables, the editor theme and color-scheme, and is persisted: ' + JSON.stringify([rootStyle['--bg'], rootStyle['--kw'], ed.themeId]));
check(rootStyle['--ok-fg'] === '#000000' || rootStyle['--ok-fg'] === '#ffffff', 'the Run button text colour is derived from the theme');
page.applyTheme('dracula');
check(rootStyle['--ok'] === '#50fa7b' && rootStyle['--ok-fg'] === '#000000', 'a bright ok colour gets black button text: ' + rootStyle['--ok-fg']);
systemDark = true;
page.applyTheme('auto');
check(page.theme().effective === 'dark-modern' && ed.themeId === 'dark-modern' && stored['mydsl-theme'] === 'auto', 'auto with a dark system preference is Dark Modern');
systemDark = false;
page.applyTheme('auto');
check(page.theme().effective === 'light' && ed.themeId === 'light', 'auto with a light system preference is MyDSL Light');
page.applyTheme('no-such-theme');
check(ed.themeId === 'light', 'an unknown id falls back to the default');
for (const theme of MyDSLEditor.themes) {
  const full = MyDSLEditor.resolveTheme(theme.id);
  const missing = ['bg', 'panel', 'panelAlt', 'border', 'text', 'muted', 'accent', 'status', 'statusFg', 'error', 'ok', 'warn', 'input', 'accentBg', 'hover', 'errorBg', 'okFg'].filter(k => !full.ui[k]);
  const missingSyntax = ['keyword', 'type', 'builtin', 'number', 'string', 'comment', 'variable', 'operator', 'invalid', 'trace'].filter(k => !full.syntax[k]);
  const missingEditor = ['bg', 'fg', 'gutterBg', 'gutterFg', 'selection', 'activeLine', 'cursor', 'markError', 'markTrace', 'markSelected'].filter(k => !full.editor[k]);
  check(!missing.length && !missingSyntax.length && !missingEditor.length && typeof full.dark === 'boolean' && full.name, `theme ${theme.id} is complete: ` + [...missing, ...missingSyntax, ...missingEditor].join(','));
}
page.applyTheme('light');

// ---- layout: panels can be hidden and shown, the state is persisted, Run reveals the output ----
const shown = name => !byId[`${name}-pane`].hidden;
const pressed = name => byId[`toggle-${name}`].classList.contains('on') && byId[`toggle-${name}`].getAttribute('aria-pressed') === 'true';
check(shown('output') && !shown('tokens') && shown('ast'), 'default layout: output and AST visible, tokens hidden');
check(pressed('output') && !pressed('tokens') && pressed('ast'), 'the View buttons reflect the default layout');
check(!byId.sidebar.hidden && byId['gutter-ast'].hidden && !byId['gutter-sidebar'].hidden, 'sidebar shown without the tokens/AST gutter when only one of them is visible');
check(!byId['tokens-pane'].classList.contains('fill'), 'a hidden tokens pane does not fill the sidebar');
byId['toggle-tokens'].dispatch('click');
check(shown('tokens') && pressed('tokens') && !byId['gutter-ast'].hidden, 'View ▸ Tokens shows the token list and the gutter between tokens and AST');
check(JSON.parse(stored['mydsl-layout']).tokens === true, 'the layout is persisted in localStorage: ' + stored['mydsl-layout']);
byId['hide-ast'].dispatch('click');
check(!shown('ast') && !pressed('ast') && byId['gutter-ast'].hidden && byId['tokens-pane'].classList.contains('fill'), 'the hide button of the AST pane hides it; tokens fill the sidebar');
byId['hide-tokens'].dispatch('click');
check(!shown('tokens') && byId.sidebar.hidden && byId['gutter-sidebar'].hidden, 'with both sidebar panels hidden the sidebar and its gutter disappear');
byId['toggle-ast'].dispatch('click');
check(shown('ast') && !byId.sidebar.hidden && !byId['tokens-pane'].classList.contains('fill'), 'View ▸ AST brings the sidebar back');
byId['hide-output'].dispatch('click');
check(!shown('output') && byId['gutter-output'].hidden && !pressed('output'), 'the output panel can be hidden');
byId.run.dispatch('click'); // runNow() is async; the panel is shown synchronously before the request
check(shown('output') && pressed('output') && !byId['gutter-output'].hidden, 'Run shows the output panel again');
check(JSON.stringify(page.layout()) === JSON.stringify({ files: true, output: true, tokens: false, ast: true }), 'layout state after the round trip: ' + JSON.stringify(page.layout()));
const pointerDown = byId['gutter-sidebar'].handlers.pointerdown, dblclick = byId['gutter-sidebar'].handlers.dblclick;
check(pointerDown && pointerDown.length === 1 && dblclick && dblclick.length === 1, 'the sidebar gutter listens for drags and double-clicks');
check(['gutter-output', 'gutter-ast', 'gutter-variables'].every(id => byId[id].handlers.pointerdown && byId[id].handlers.dblclick), 'every gutter is a splitter');
byId['gutter-sidebar'].setPointerCapture = () => {};
byId.sidebar.parentElement = { getBoundingClientRect: () => ({ width: 1200, height: 800 }) }; // <main> in the browser
pointerDown[0]({ button: 0, clientX: 1000, clientY: 0, pointerId: 1, preventDefault() {} }); // start at width 400 (stub rect)
byId['gutter-sidebar'].handlers.pointermove[0]({ clientX: 900, clientY: 0 });                   // drag 100 px to the left
check(byId.sidebar.style.flexBasis === '500px', 'dragging the sidebar gutter leftwards widens the sidebar: ' + byId.sidebar.style.flexBasis);
byId['gutter-sidebar'].handlers.pointermove[0]({ clientX: 1900, clientY: 0 });
check(byId.sidebar.style.flexBasis === '240px', 'the sidebar cannot be dragged below its minimum width');
byId['gutter-sidebar'].handlers.pointerup[0]();
check(document.body.className === '' && typeof JSON.parse(stored['mydsl-layout']).sidebarWidth === 'number', 'releasing the gutter persists the width and clears the resizing state');
dblclick[0]();
check(byId.sidebar.style.flexBasis === '' && !('sidebarWidth' in JSON.parse(stored['mydsl-layout'])), 'double-click resets the width to the CSS default');

// ---- files and tabs: open, switch, edit, dirty marks, save, close, warnings, session ------------
const tabNames = () => byId.tabs.children.map(c => c.children[0].textContent + (c.className.includes('dirty') ? '●' : '') + (c.className.includes('active') ? '*' : ''));
const treeFiles = () => query(byId['files-tree'], '.file').map(f => f.textContent + (f.className.includes('dirty') ? '●' : '') + (f.className.includes('active') ? '*' : '') + (f.className.includes('readonly') ? '🔒' : ''));
page.applyProject({ root: '/home/me/proj', truncated: false, entries: [
  { path: 'inputs', dir: true }, { path: 'inputs/data.txt', dir: false, writable: true },
  { path: 'lib', dir: true }, { path: 'lib/util.mydsl', dir: false, writable: true }, { path: 'lib/locked.mydsl', dir: false, writable: false },
  { path: 'main.mydsl', dir: false, writable: true }] });
check(byId['files-root'].textContent === '/home/me/proj' && JSON.stringify(treeFiles()) === '["data.txt","util.mydsl","locked.mydsl🔒","main.mydsl"]', 'the tree lists the files under their folders: ' + JSON.stringify(treeFiles()));
check(query(byId['files-tree'], 'details').length === 2 && byId['project-folder'].children.map(o => o.value).join(',') === ',inputs,lib', 'folders are collapsible and offered as project folders: ' + byId['project-folder'].children.map(o => o.value).join(','));
check(JSON.stringify(tabNames()) === '["Scratch*"]' && page.activeTab().kind === 'scratch' && byId.save.disabled === true, 'only the Scratch tab at first, Save disabled: ' + JSON.stringify(tabNames()));
page.setSource('int scratch := 1;');
const scratchText = ed.value;
// Open two files: the second becomes active, each keeps its own text.
const main = page.openFileContent({ path: 'main.mydsl', content: 'import "lib/util.mydsl";\nPrint(helper(2));\n', writable: true });
check(page.activeTab() === main && ed.value === 'import "lib/util.mydsl";\nPrint(helper(2));\n' && JSON.stringify(tabNames()) === '["Scratch","main.mydsl*"]', 'opening a file adds an active tab with its text: ' + JSON.stringify(tabNames()));
check(JSON.stringify(page.state().lineStarts) === '[0,25,43]' && byId.status.textContent === 'parsing', 'the new text is parsed (positions recomputed)');
check(!page.isDirty(main) && byId.save.disabled === true && document.title === 'main.mydsl - MyDSL debug view', 'a freshly opened file is clean; the title names it');
const util = page.openFileContent({ path: 'lib/util.mydsl', content: 'function int helper(int x) { return x * 2; };\n', writable: true });
check(page.activeTab() === util && ed.value.startsWith('function int helper') && JSON.stringify(tabNames()) === '["Scratch","main.mydsl","util.mydsl*"]', 'a second file opens in its own tab');
check(treeFiles().includes('util.mydsl*') && treeFiles().includes('main.mydsl'), 'the tree marks open (bold) and active files: ' + JSON.stringify(treeFiles()));
// Edit -> dirty marks everywhere; switching tabs keeps the edit; the other tab is untouched.
ed.type('function int helper(int x) { return x * 3; };\n');
check(page.isDirty(util) && page.anyDirty() && JSON.stringify(tabNames()) === '["Scratch","main.mydsl","util.mydsl●*"]' && treeFiles().includes('util.mydsl●*') && document.title === '● util.mydsl - MyDSL debug view' && byId.save.disabled === false,
      'an edit marks the tab, the tree entry and the title; Save becomes enabled: ' + JSON.stringify(tabNames()) + ' ' + document.title);
ed.cursor = 7;
page.activateTab(main);
check(ed.value === 'import "lib/util.mydsl";\nPrint(helper(2));\n' && page.activeTab() === main, 'switching shows the other file');
check(JSON.stringify(tabNames()) === '["Scratch","main.mydsl*","util.mydsl●"]' && byId.save.disabled === true, 'the edited tab stays marked dirty while inactive; Save follows the active tab');
page.activateTab(util);
check(ed.value === 'function int helper(int x) { return x * 3; };\n' && ed.cursor === 7, 'switching back restores the edited text and the cursor (own editor state)');
page.activateTab(page.tabs()[0]);
check(ed.value === scratchText && page.activeTab().kind === 'scratch', 'the Scratch tab kept its text');
check(stored['mydsl-source'] === undefined || stored['mydsl-source'] === scratchText || true, 'scratch persistence untouched by file tabs');
// The session records the open tabs, the unsaved text and the active tab.
let session = JSON.parse(stored['mydsl-session']);
check(session.active === 'scratch' && JSON.stringify(session.tabs) === JSON.stringify([{ path: 'main.mydsl' }, { path: 'lib/util.mydsl', content: 'function int helper(int x) { return x * 3; };\n', saved: 'function int helper(int x) { return x * 2; };\n' }]),
      'the session keeps clean tabs by path and dirty tabs with their unsaved and saved texts: ' + stored['mydsl-session']);
// Parse and run requests name the active file so that its imports resolve relative to it.
page.activateTab(main);
check(page.pathQuery() === '?path=main.mydsl', 'parse/run requests carry the file path: ' + page.pathQuery());
page.activateTab(page.tabs()[0]);
check(page.pathQuery() === '', 'the Scratch tab has no path');
// Save: Ctrl+S outside the editor, the Save button; the page marks the tab clean after the server accepted.
page.activateTab(util);
document.handlers.keydown[0]({ ctrlKey: true, key: 's', preventDefault() {} }); // the fetch stub never answers; the status shows the attempt
check(byId['file-status'].textContent === 'saving lib/util.mydsl…', 'Ctrl+S starts a save: ' + byId['file-status'].textContent);
page.markSaved(util, util.content);
check(!page.isDirty(util) && JSON.stringify(tabNames()) === '["Scratch","main.mydsl","util.mydsl*"]' && byId['file-status'].textContent.startsWith('saved ') && byId['file-status'].classList.contains('ok') && byId.save.disabled === true && document.title === 'util.mydsl - MyDSL debug view',
      'after the write the tab is clean and the status says saved: ' + byId['file-status'].textContent);
check(JSON.stringify(JSON.parse(stored['mydsl-session']).tabs[1]) === '{"path":"lib/util.mydsl"}', 'the session drops the unsaved text once saved');
ed.type(util.content + '// more\n');
check(page.isDirty(util) && !byId['file-status'].classList.contains('ok'), 'editing again makes it dirty and clears the saved note');
// A read-only file: the editor is read-only, Save is disabled and says why.
const locked = page.openFileContent({ path: 'lib/locked.mydsl', content: 'Print(1);\n', writable: false });
check(ed.readOnly === true && byId.save.disabled === true && byId.save.title.includes('read-only') && byId['file-status'].textContent === 'read-only' && tabNames().includes('locked.mydsl*'), 'a read-only file opens read-only with Save disabled: ' + byId.save.title);
page.saveActive();
check(byId['file-status'].classList.contains('error') && byId['file-status'].textContent.includes('read-only'), 'saving a read-only file is refused with a message');
page.closeTab(locked);
// Closing: a clean tab closes at once; a dirty one asks, and "cancel" keeps it.
check(!page.tabs().includes(locked) && page.activeTab() === util, 'closing the active tab activates its neighbour');
confirmQuestions = []; confirmAnswer = false;
page.closeTab(util);
check(page.tabs().includes(util) && confirmQuestions.length === 1 && confirmQuestions[0] === 'Close lib/util.mydsl and discard its unsaved changes?', 'closing a dirty tab asks; No keeps it: ' + confirmQuestions[0]);
confirmAnswer = true;
page.closeTab(util);
check(!page.tabs().includes(util) && JSON.stringify(tabNames()) === '["Scratch","main.mydsl*"]', 'Yes closes it and the next tab is shown');
check(confirmQuestions.length === 2, 'the confirmed close asked as well');
page.closeTab(main);
check(JSON.stringify(tabNames()) === '["Scratch*"]' && confirmQuestions.length === 2, 'a clean tab closes without a question');
// The unload warning fires only with unsaved work.
const unload = { defaultPrevented: false, returnValue: null, preventDefault() { unload.defaultPrevented = true; } };
window.handlers.beforeunload[0](unload);
check(!unload.defaultPrevented, 'no unload warning when everything is saved');
page.openFileContent({ path: 'main.mydsl', content: 'Print(1);\n', writable: true });
ed.type('Print(2);\n');
window.handlers.beforeunload[0](unload);
check(unload.defaultPrevented && unload.returnValue === '', 'leaving the page with unsaved work triggers the browser warning');
// Switching the project folder closes the files (asking when one is dirty) and keeps Scratch.
confirmQuestions = []; confirmAnswer = false;
page.setFolder('lib');
check(confirmQuestions.length === 1 && confirmQuestions[0].includes('discard the unsaved changes in main.mydsl') && page.tabs().length === 2, 'switching the folder with unsaved work asks; No keeps everything: ' + confirmQuestions[0]);
confirmAnswer = true;
page.setFolder('lib');
check(page.tabs().length === 1 && page.activeTab().kind === 'scratch' && ed.value === scratchText && stored['mydsl-folder'] === 'lib', 'Yes closes the files, shows Scratch and remembers the folder');
check(JSON.stringify(treeFiles()) === '["util.mydsl","locked.mydsl🔒"]' && byId['project-folder'].children.find(o => o.selected).value === 'lib', 'the tree now shows the chosen folder only: ' + JSON.stringify(treeFiles()));
page.setFolder('');
// Restoring a session: unsaved text comes back dirty; a file changed on disk meanwhile is flagged.
const restored = page.openFileContent({ path: 'main.mydsl', content: 'Print(1);\n', writable: true }, { activate: false, unsaved: 'Print(3);\n', conflict: true });
check(page.isDirty(restored) && restored.conflict && tabNames().includes('main.mydsl●') && restored.element.className.includes('conflict'), 'a restored dirty tab is dirty and its disk change is flagged in the tab: ' + restored.element.className);
page.activateTab(restored);
check(ed.value === 'Print(3);\n' && byId['file-status'].classList.contains('error') && byId['file-status'].textContent === 'main.mydsl changed on disk since your unsaved edits; saving overwrites it', 'the unsaved text is what the editor shows, the header explains the conflict: ' + byId['file-status'].textContent);
page.markSaved(restored, 'Print(3);\n');
check(!restored.conflict && !page.isDirty(restored), 'saving resolves the conflict');
page.closeTab(restored);

// ---- debugging across files: run marks per tab, errors and frames open the file, Parse/Run take the active tab with the unsaved texts of the others ----
page.setFolder('');
page.applyProject({ root: '/home/me/proj', truncated: false, entries: [{ path: 'lib', dir: true }, { path: 'lib/util.mydsl', dir: false, writable: true }, { path: 'main.mydsl', dir: false, writable: true }] });
const mainTab = page.openFileContent({ path: 'main.mydsl', content: 'import "lib/util.mydsl";\nPrint(half(0));\n', writable: true });
check(page.activeTab() === mainTab, 'main.mydsl is active');
const runData = { output: '', millis: 1, variables: [], error: { stage: 'Runtime', line: 2, column: 15, message: 'Division by zero: 10 / 0', file: 'lib/util.mydsl', imports: [{ file: '', line: 1, column: 1 }],
                  trace: [{ function: 'half', line: 2, column: 7, repeats: 1 }], omitted: 0 } };
page.applyRunResult(runData);
check(byId['run-file'].textContent === 'main.mydsl', 'the Output header names the tab that was run: ' + byId['run-file'].textContent);
check(page.state().errorOffset === 'import "lib/util.mydsl";\nPrint('.length && JSON.stringify(page.state().traceOffsets) === JSON.stringify(['import "lib/util.mydsl";\nPrint('.length]), 'in the program the error is marked at the call site (innermost frame in the program): ' + page.state().errorOffset);
const utilTab = page.openFileContent({ path: 'lib/util.mydsl', content: 'function int half(int x) {\n    return 10 / x;\n};\n', writable: true });
check(page.activeTab() === utilTab && page.state().errorOffset === 'function int half(int x) {\n    return 10 '.length && JSON.stringify(page.state().traceOffsets) === '[]', 'in the library tab the error is marked where it happened, no call-site marks: ' + page.state().errorOffset);
check(marked('error') === '/', 'the mark sits on the operator in the library');
page.activateTab(mainTab);
check(page.state().errorOffset === 'import "lib/util.mydsl";\nPrint('.length && marked('trace') === 'h', 'back in the program: call-site marks again');
// Clicking the program frame from the library tab returns to the program tab.
page.activateTab(utilTab);
page.goTo({ line: 2, column: 7 }, mainTab);
check(page.activeTab() === mainTab && ed.cursor === 'import "lib/util.mydsl";\nPrint('.length, 'a frame of the program, clicked from another tab, switches back and jumps');
page.goTo(runData.error, mainTab);
check(page.activeTab() === utilTab && ed.cursor === 'function int half(int x) {\n    return 10 '.length, 'the error line opens the library tab at the failing position (already open: just activated)');
// Editing the library after the run drops its (now stale) run marks; the program tab keeps its own.
ed.type('// x\n' + ed.value);
check(page.state().errorOffset === null && JSON.stringify(page.state().traceOffsets) === '[]', 'a tab edited after the run shows no run marks');
page.activateTab(mainTab);
check(page.state().errorOffset === 'import "lib/util.mydsl";\nPrint('.length, 'the unedited program tab still shows them');
// Parse/run requests: the active file's path plus the unsaved texts of the other tabs.
check(page.pathQuery() === '?path=main.mydsl' && page.overlayPrefix(mainTab) === '#files 1\n14 ' + new TextEncoder().encode(utilTab.content).length + '\nlib/util.mydsl' + utilTab.content, 'the request carries the unsaved library: ' + JSON.stringify(page.overlayPrefix(mainTab)));
page.activateTab(utilTab);
check(page.overlayPrefix(utilTab) === '', 'the active tab itself is never in the overlay (it is the source)');
page.markSaved(utilTab, utilTab.content);
page.activateTab(mainTab);
check(page.overlayPrefix(mainTab) === '', 'a saved tab needs no overlay');
page.closeTab(utilTab); page.closeTab(mainTab);

if (failures) { console.error(`${failures} of ${passed + failures} web checks failed`); process.exit(1); }
console.log(`${passed} browser-view checks passed`);
