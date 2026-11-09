// MyDSL for CodeMirror 6: a stream tokenizer that mirrors src/lexer.cpp (comments, words,
// numbers, strings with their escapes, symbols) plus the keyword/builtin lists of
// src/parser.cpp and src/type_checker.cpp, and an indentation rule for braces, brackets and
// parentheses. Only for highlighting and indentation: the real tokens come from the C++ lexer
// through /api/parse.
import { StreamLanguage, indentUnit } from '@codemirror/language';

export const TYPE_KEYWORDS = ['int', 'float', 'bool', 'string', 'intvector', 'floatvector', 'intmatrix', 'floatmatrix'];
export const KEYWORDS = ['if', 'then', 'else', 'while', 'for', 'function', 'return', 'import', 'error', 'Print'];
export const CONSTANTS = ['true', 'false'];
export const BUILTINS = ['abs', 'angle_vec', 'determinant', 'dotprod_vec', 'Input', 'inv_vec', 'inverse_mat',
                         'is_zero', 'length', 'mult_mat', 'substring', 'to_float', 'to_int', 'to_string', 'transpose'];

const typeSet = new Set(TYPE_KEYWORDS), keywordSet = new Set(KEYWORDS), constantSet = new Set(CONSTANTS), builtinSet = new Set(BUILTINS);
const isWordStart = ch => /[A-Za-z_']/.test(ch);
const isWordChar = ch => /[A-Za-z0-9_']/.test(ch);
const isDigit = ch => /[0-9]/.test(ch);

// The state: `inComment` inside a /* */ comment, `depth` = number of open ( [ { at the start
// of the line being indented.
const mydslParser = {
  name: 'mydsl',
  startState: () => ({ inComment: false, depth: 0 }),
  copyState: s => ({ inComment: s.inComment, depth: s.depth }),
  token(stream, state) {
    if (state.inComment) {
      if (stream.match(/^.*?\*\//)) state.inComment = false; else stream.skipToEnd();
      return 'comment';
    }
    if (stream.eatSpace()) return null;
    if (stream.match('//')) { stream.skipToEnd(); return 'comment'; }
    if (stream.match('/*')) { state.inComment = true; if (stream.match(/^.*?\*\//)) state.inComment = false; else stream.skipToEnd(); return 'comment'; }
    const ch = stream.peek();
    if (isWordStart(ch)) {
      let word = '';
      while (!stream.eol() && isWordChar(stream.peek())) word += stream.next();
      if (typeSet.has(word)) return 'typeName';
      if (keywordSet.has(word)) return 'keyword';
      if (constantSet.has(word)) return 'bool';
      if (builtinSet.has(word)) return 'variableName.standard';
      if (stream.match(/^\s*\(/, false)) return 'variableName.function';
      return 'variableName';
    }
    if (isDigit(ch) || (ch === '.' && isDigit(stream.string.charAt(stream.pos + 1)))) {
      stream.match(/^[0-9]*(\.[0-9]*)?([eE][+-]?[0-9]+)?/);
      return 'number';
    }
    if (ch === '"') {
      stream.next();
      for (;;) {
        if (stream.eol()) return 'invalid';             // unterminated: a lexing error
        const c = stream.next();
        if (c === '"') return 'string';
        if (c === '\\') {
          if (stream.eol()) return 'invalid';
          const e = stream.next();
          if (!'"\\ntr'.includes(e)) { stream.skipToEnd(); return 'invalid'; } // unknown escape: a lexing error
        }
      }
    }
    if (stream.match(/^(:=|==|!=|<=|>=|&&|\|\|)/)) return 'operator';
    if (stream.match(/^[+\-*\/%<>!=]/)) return 'operator';
    if (stream.match(/^[([{]/)) { state.depth++; return 'bracket'; }
    if (stream.match(/^[)\]}]/)) { state.depth = Math.max(0, state.depth - 1); return 'bracket'; }
    if (stream.match(/^[,;.]/)) return 'punctuation';
    stream.next();
    return 'invalid'; // anything else is a lexing error ("Unexpected character")
  },
  // Indentation of a new line: one unit per open bracket, one less when the line starts by
  // closing one. Used on Enter and when a closing bracket is typed (electricInput).
  indent(state, textAfter, context) {
    const closes = /^\s*[)\]}]/.test(textAfter) ? 1 : 0;
    return Math.max(0, state.depth - closes) * context.unit;
  },
  languageData: {
    commentTokens: { line: '//', block: { open: '/*', close: '*/' } },
    closeBrackets: { brackets: ['(', '[', '{', '"'] },
    indentOnInput: /^\s*[)\]}]$/,
  },
};

export const mydslLanguage = StreamLanguage.define(mydslParser);

// The language support without colours: the highlight style comes with the theme (themes.js).
export function mydsl() {
  return [mydslLanguage, indentUnit.of('    ')];
}

// Templates inserted when a statement keyword is accepted from the completion list (CodeMirror
// snippet syntax: ${n:placeholder} fields, Tab moves between them; "\t" = one indent unit).
export const SNIPPETS = {
  if: 'if (${1:condition}) then {\n\t${2}\n};',
  while: 'while (${1:condition}) {\n\t${2}\n};',
  for: 'for (${1:i} := ${2:0}, ${1:i} < ${3:n}, ${4:1}) {\n\t${5}\n};',
  function: 'function ${1:int} ${2:name}(${3:int x}) {\n\t${4}\n\treturn ${5:value};\n};',
  import: 'import "${1:file.mydsl}";',
  Print: 'Print(${1:value});',
  error: 'error("${1:message}");',
};
