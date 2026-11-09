// Checks the source editor (CodeMirror 6 with the MyDSL mode, web/editor/src) in a real
// browser: syntax highlighting, line numbers, automatic indentation, bracket matching and
// auto-closing, undo/redo, the keyboard shortcuts, and that the page's own features - Parse,
// Run, error markers, token/AST highlighting, the variable inspector, persistence - work with
// it. With the binary as second argument it also starts a server of its own in a throwaway
// project directory and checks the file panel: opening, editing, saving, switching tabs,
// unsaved marks and warnings, reloads, conflicts, read-only files, imports between project
// files. Needs Playwright (npm install -g playwright && npx playwright install chromium); the
// server must be running:   node tests/editor.test.js http://127.0.0.1:8000 [./mydsl]
'use strict';
let playwright = null;
for (const candidate of ['playwright', () => require('path').join(require('child_process').execSync('npm root -g', { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).trim(), 'playwright')]) {
  try { playwright = require(typeof candidate === 'function' ? candidate() : candidate); break; } catch (e) { /* try the next one */ }
}
if (!playwright) {
  console.error('playwright not found: skipping the editor checks (npm install -g playwright && npx playwright install chromium)');
  process.exit(0);
}
const url = process.argv[2] || 'http://127.0.0.1:8000';
// CodeMirror binds "Mod" to Cmd on macOS (the browser reports MacIntel there) and to Ctrl
// elsewhere: undo, redo, find, comment etc. follow the platform. The page's own shortcuts
// (parse, run, save) accept both, but these checks press the platform's key like a user would.
const mod = process.platform === 'darwin' ? 'Meta' : 'Control';
const exe = process.argv[3] ? require('path').resolve(process.argv[3]) : null;
const fs = require('fs'), path = require('path'), os = require('os'), { spawn } = require('child_process');

let failures = 0, passed = 0;
function check(ok, name, detail = '') {
  if (ok) { ++passed; return; }
  ++failures;
  console.error(`FAIL: ${name}${detail ? '\n      ' + detail : ''}`);
}

// The project: main.mydsl imports lib/util.mydsl; a data file; things that must not show up
// (a hidden file, a file with another extension, a symbolic link to a file outside, the
// build/ directory); a read-only file.
function makeProject() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'mydsl-project-'));
  fs.mkdirSync(path.join(dir, 'lib'));
  fs.mkdirSync(path.join(dir, 'build'));
  fs.writeFileSync(path.join(dir, 'main.mydsl'), 'import "lib/util.mydsl";\nint n := Input();\nPrint(twice(n));\n');
  fs.writeFileSync(path.join(dir, 'lib', 'util.mydsl'), 'function int twice(int x) {\n    return x * 2;\n};\n');
  fs.writeFileSync(path.join(dir, 'lib', 'frozen.mydsl'), 'Print("read-only");\n');
  fs.chmodSync(path.join(dir, 'lib', 'frozen.mydsl'), 0o444);
  fs.writeFileSync(path.join(dir, 'data.txt'), '21\n');
  fs.mkdirSync(path.join(dir, 'calc')); // nested imports: main -> ops -> core
  fs.writeFileSync(path.join(dir, 'calc', 'core.mydsl'), 'function int divide(int a, int b) {\n    if (b == 0) then {\n        error("divide: b is zero");\n    };\n    return a / b;\n};\n');
  fs.writeFileSync(path.join(dir, 'calc', 'ops.mydsl'), 'import "core.mydsl";\nfunction int ratio(int a, int b) {\n    return divide(a, b) * 100;\n};\n');
  fs.writeFileSync(path.join(dir, 'calc', 'main.mydsl'), 'import "ops.mydsl";\nPrint(ratio(10, 2));\nPrint(ratio(1, 0));\n');
  fs.writeFileSync(path.join(dir, '.secret.mydsl'), 'Print("hidden");\n');
  fs.writeFileSync(path.join(dir, 'notes.md'), '# not a MyDSL file\n');
  fs.writeFileSync(path.join(dir, 'build', 'generated.mydsl'), 'Print("build");\n');
  const outside = path.join(os.tmpdir(), `mydsl-outside-${process.pid}.mydsl`);
  fs.writeFileSync(outside, 'Print("outside");\n');
  fs.symlinkSync(outside, path.join(dir, 'lib', 'escape.mydsl'));
  return { dir, outside };
}
function startServer(cwd) {
  return new Promise((resolve, reject) => {
    const child = spawn(exe, ['--serve', '0'], { cwd, stdio: ['ignore', 'ignore', 'pipe'] });
    let log = '';
    child.stderr.on('data', chunk => { log += chunk; const m = /http:\/\/127\.0\.0\.1:(\d+)\//.exec(log); if (m) resolve({ child, url: `http://127.0.0.1:${m[1]}` }); });
    child.on('exit', code => reject(new Error(`server exited with ${code}: ${log}`)));
  });
}
async function fileChecks(browser) {
  const { dir, outside } = makeProject();
  const server = await startServer(dir);
  const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
  const problems = [], dialogs = [];
  let dialogAnswer = true;
  page.on('pageerror', e => problems.push(`page error: ${e.message}`));
  page.on('console', m => { if (m.type() === 'error' && !/Failed to load resource/.test(m.text())) problems.push(`console error: ${m.text()}`); }); // the refused requests below are logged as resource errors
  page.on('dialog', async d => { dialogs.push({ type: d.type(), message: d.message() }); if (dialogAnswer) await d.accept(); else await d.dismiss(); });
  const text = () => page.evaluate(() => window.mydslDebug.editor().getValue());
  const tabNames = () => page.$$eval('#tabs .tab', els => els.map(e => e.querySelector('.name').textContent + (e.classList.contains('dirty') ? '●' : '') + (e.classList.contains('active') ? '*' : '')));
  const treeFiles = () => page.$$eval('#files-tree .file', els => els.map(e => e.title.replace(/ \(read-only\)$/, '') + (e.classList.contains('dirty') ? '●' : '') + (e.classList.contains('readonly') ? '🔒' : '')));
  const openFromTree = async p => { await page.click(`#files-tree .file[title^="${p}"]`); await page.waitForFunction(p => window.mydslDebug.activeTab().path === p, p); await page.waitForTimeout(250); };
  const fileStatus = () => page.textContent('#file-status');
  const onDisk = p => fs.readFileSync(path.join(dir, p), 'utf8');
  const typeAtEnd = async s => { await page.evaluate(() => { const ed = window.mydslDebug.editor(); ed.setCursor(ed.getValue().length); ed.focus(); }); await page.keyboard.type(s); await page.waitForTimeout(200); };
  const setText = async s => { await page.evaluate(s => window.mydslDebug.editor().setValue(s), s); await page.waitForTimeout(350); };
  try {
    await page.goto(server.url);
    await page.waitForSelector('.cm-editor .cm-content');
    await page.evaluate(() => localStorage.clear());
    await page.reload();
    await page.waitForSelector('#files-tree .file');
    // The tree: only .mydsl/.txt files below the server directory; hidden, other extensions, build/ and the escaping link are absent.
    check((await page.textContent('#files-root')) === fs.realpathSync(dir), 'the panel names the server directory: ' + await page.textContent('#files-root'));
    const tree = await treeFiles();
    check(JSON.stringify(tree) === JSON.stringify(['calc/core.mydsl', 'calc/main.mydsl', 'calc/ops.mydsl', 'data.txt', 'lib/frozen.mydsl🔒', 'lib/util.mydsl', 'main.mydsl']), 'the tree lists the project files, the read-only one marked; hidden/.md/build/symlink excluded: ' + JSON.stringify(tree));
    check(JSON.stringify(await tabNames()) === '["Scratch*"]' && (await page.textContent('#source-path')) === 'Scratch (browser only)', 'the Scratch tab is active at first');
    // Open, run: the import of main.mydsl resolves relative to the file (the request names it).
    await openFromTree('main.mydsl');
    check(await text() === 'import "lib/util.mydsl";\nint n := Input();\nPrint(twice(n));\n' && (await page.textContent('#source-path')) === 'main.mydsl', 'clicking a file opens it: ' + await page.textContent('#source-path'));
    check((await page.textContent('#status')).startsWith('ok ·'), 'main.mydsl type-checks: its import is found next to it: ' + await page.textContent('#status'));
    await page.fill('#stdin', '21');
    await page.click('#run');
    await page.waitForFunction(() => document.getElementById('run-status').textContent.startsWith('finished'));
    check((await page.textContent('#output')) === 'Int(42)\n', 'Run executes the file with its imports: ' + JSON.stringify(await page.textContent('#output')));
    // Edit the library in a second tab: dirty marks; nothing is written until Save.
    await openFromTree('lib/util.mydsl');
    check(JSON.stringify(await tabNames()) === '["Scratch","main.mydsl","util.mydsl*"]', 'a second tab: ' + JSON.stringify(await tabNames()));
    await page.evaluate(() => { const ed = window.mydslDebug.editor(); ed.setCursor(ed.getValue().indexOf('* 2') + 3); ed.focus(); });
    await page.keyboard.press('Backspace'); await page.keyboard.type('3');
    await page.waitForTimeout(300);
    check(await text() === 'function int twice(int x) {\n    return x * 3;\n};\n', 'edited the library: ' + JSON.stringify(await text()));
    check(JSON.stringify(await tabNames()) === '["Scratch","main.mydsl","util.mydsl●*"]' && (await treeFiles()).includes('lib/util.mydsl●') && (await page.title()) === '● util.mydsl - MyDSL debug view' && (await page.textContent('#source-path')) === 'lib/util.mydsl ●',
          'the unsaved edit is marked in the tab, the tree, the title and the header: ' + JSON.stringify(await tabNames()) + ' ' + await page.title());
    check(await page.$eval('#save', b => !b.disabled), 'Save is enabled for a dirty file');
    check(onDisk('lib/util.mydsl').includes('x * 2'), 'nothing is written before Save');
    await page.click('#tabs .tab:nth-child(2)');
    await page.waitForTimeout(250);
    await page.click('#run');
    await page.waitForFunction(() => document.getElementById('run-status').textContent.startsWith('finished'));
    check((await page.textContent('#output')) === 'Int(63)\n' && (await page.textContent('#run-status')).includes('with the unsaved text of lib/util.mydsl') && onDisk('lib/util.mydsl').includes('x * 2'), 'running main while the library is unsaved uses the editor\'s text of the library and says so: ' + await page.textContent('#run-status'));
    await page.click('#tabs .tab:nth-child(3)');
    await page.waitForTimeout(250);
    // Switch tabs and back: the edit, cursor and undo history are per tab.
    await page.click('#tabs .tab:nth-child(2)');
    await page.waitForTimeout(250);
    check((await text()).startsWith('import') && JSON.stringify(await tabNames()) === '["Scratch","main.mydsl*","util.mydsl●"]' && await page.$eval('#save', b => b.disabled), 'switching shows main.mydsl (clean, Save disabled); util stays marked');
    await page.click('.cm-content');
    await page.keyboard.press(`${mod}+z`);
    check((await text()).startsWith('import "lib/util.mydsl";\nint n := Input();'), 'Ctrl+Z in main.mydsl does not touch the other tab\'s history');
    await page.click('#tabs .tab:nth-child(3)');
    await page.waitForTimeout(250);
    check(await text() === 'function int twice(int x) {\n    return x * 3;\n};\n' && await page.evaluate(() => window.mydslDebug.editor().getCursor()) === 'function int twice(int x) {\n    return x * 3'.length, 'back in util.mydsl: the edit and the cursor are where they were');
    await page.click('.cm-content');
    await page.keyboard.press(`${mod}+z`);
    check(await text() === 'function int twice(int x) {\n    return x * 2;\n};\n' && JSON.stringify(await tabNames()) === '["Scratch","main.mydsl","util.mydsl*"]', 'undo works per tab, and undoing back to the disk text makes the tab clean again');
    await page.keyboard.press(`${mod}+Shift+Z`);
    check(await text() === 'function int twice(int x) {\n    return x * 3;\n};\n', 'redo');
    // Reload with unsaved work: the browser warns, and the session restores the tabs, the unsaved text and the active tab.
    dialogs.length = 0;
    await page.reload();
    await page.waitForSelector('.cm-editor .cm-content');
    await page.waitForFunction(() => window.mydslDebug.tabs().length === 3);
    await page.waitForTimeout(300);
    check(dialogs.length === 1 && dialogs[0].type === 'beforeunload', 'leaving with unsaved changes shows the browser\'s warning: ' + JSON.stringify(dialogs));
    check(JSON.stringify(await tabNames()) === '["Scratch","main.mydsl","util.mydsl●*"]' && await text() === 'function int twice(int x) {\n    return x * 3;\n};\n', 'after the reload the tabs are back, the edit is kept and marked: ' + JSON.stringify(await tabNames()));
    // Save with Ctrl+S: the file changes on disk, the tab is clean, the status says so; Run sees the new library.
    await page.click('.cm-content');
    await page.keyboard.press(`${mod}+s`);
    await page.waitForFunction(() => document.getElementById('file-status').textContent.startsWith('saved'));
    check(onDisk('lib/util.mydsl') === 'function int twice(int x) {\n    return x * 3;\n};\n', 'Ctrl+S wrote the file');
    check(JSON.stringify(await tabNames()) === '["Scratch","main.mydsl","util.mydsl*"]' && (await page.title()) === 'util.mydsl - MyDSL debug view' && await page.$eval('#save', b => b.disabled), 'after saving nothing is marked');
    check(!fs.existsSync(path.join(dir, 'lib', 'util.mydsl.mydsl-saving')), 'no temporary file is left behind');
    await page.click('#tabs .tab:nth-child(2)');
    await page.waitForTimeout(250);
    await page.click('#run');
    await page.waitForFunction(() => document.getElementById('run-status').textContent.startsWith('finished'));
    check((await page.textContent('#output')) === 'Int(63)\n' && !(await page.textContent('#run-status')).includes('unsaved'), 'Run of main.mydsl uses the saved library: ' + JSON.stringify(await page.textContent('#output')));
    // The Save button writes the active file.
    await typeAtEnd('Print(1);\n');
    await page.click('#save');
    await page.waitForFunction(() => document.getElementById('file-status').textContent.startsWith('saved'));
    check(onDisk('main.mydsl').endsWith('Print(twice(n));\nPrint(1);\n'), 'the Save button writes the active file: ' + JSON.stringify(onDisk('main.mydsl')));
    // A read-only file: opened read-only, typing does nothing, Save disabled with the reason.
    await openFromTree('lib/frozen.mydsl');
    await page.click('.cm-content');
    await page.keyboard.type('xyz');
    check(await text() === 'Print("read-only");\n' && (await fileStatus()) === 'read-only' && await page.$eval('#save', b => b.disabled && b.title.includes('read-only')), 'a read-only file cannot be edited and says so: ' + await page.$eval('#save', b => b.title));
    check(JSON.stringify(await tabNames()) === '["Scratch","main.mydsl","util.mydsl","frozen.mydsl*"]' && await page.$eval('#tabs .tab.active', e => e.classList.contains('readonly')), 'the tab is marked read-only');
    // A write refused by the server (the file became read-only after it was opened) is reported and the tab stays dirty.
    await page.click('#tabs .tab:nth-child(3)');
    await page.waitForTimeout(250);
    fs.chmodSync(path.join(dir, 'lib', 'util.mydsl'), 0o444);
    await typeAtEnd('// late\n');
    await page.keyboard.press(`${mod}+s`);
    await page.waitForFunction(() => document.getElementById('file-status').textContent.startsWith('Cannot save'));
    check((await fileStatus()).includes('Permission denied') && JSON.stringify(await tabNames()) === '["Scratch","main.mydsl","util.mydsl●*","frozen.mydsl"]' && !onDisk('lib/util.mydsl').includes('late'),
          'a refused save shows the server\'s reason and keeps the tab dirty: ' + await fileStatus());
    fs.chmodSync(path.join(dir, 'lib', 'util.mydsl'), 0o644);
    await page.keyboard.press(`${mod}+s`);
    await page.waitForFunction(() => document.getElementById('file-status').textContent.startsWith('saved'));
    check(onDisk('lib/util.mydsl').includes('// late'), 'once writable again the save goes through');
    // Closing tabs: × on a clean tab closes at once; on a dirty tab it asks; Cancel keeps it, OK closes it.
    await page.hover('#tabs .tab:nth-child(4)'); await page.click('#tabs .tab:nth-child(4) .close'); // frozen (clean)
    await page.waitForTimeout(150);
    check(JSON.stringify(await tabNames()) === '["Scratch","main.mydsl","util.mydsl*"]' && dialogs.length === 1, 'a clean tab closes without a question');
    await typeAtEnd('// again\n');
    dialogAnswer = false;
    await page.hover('#tabs .tab:nth-child(3)'); await page.click('#tabs .tab:nth-child(3) .close');
    await page.waitForTimeout(150);
    check(dialogs.length === 2 && dialogs[1].type === 'confirm' && dialogs[1].message === 'Close lib/util.mydsl and discard its unsaved changes?' && (await tabNames()).includes('util.mydsl●*'), 'closing a dirty tab asks; Cancel keeps it: ' + JSON.stringify(dialogs[1]));
    dialogAnswer = true;
    await page.hover('#tabs .tab:nth-child(3)'); await page.click('#tabs .tab:nth-child(3) .close');
    await page.waitForTimeout(150);
    check(JSON.stringify(await tabNames()) === '["Scratch","main.mydsl*"]' && onDisk('lib/util.mydsl').endsWith('// late\n'), 'OK closes it without saving; main.mydsl becomes active');
    // Switching the project folder with unsaved work asks; it closes the files and shows the folder.
    await typeAtEnd('// x\n');
    dialogAnswer = false;
    await page.selectOption('#project-folder', 'lib');
    await page.waitForTimeout(150);
    check(dialogs.length === 4 && dialogs[3].message === 'Switch to lib and discard the unsaved changes in main.mydsl?' && (await tabNames()).includes('main.mydsl●*') && await page.$eval('#project-folder', s => s.value) === '', 'switching the folder with unsaved work asks; Cancel keeps everything: ' + JSON.stringify(dialogs));
    dialogAnswer = true;
    await page.selectOption('#project-folder', 'lib');
    await page.waitForTimeout(250);
    check(JSON.stringify(await tabNames()) === '["Scratch*"]' && JSON.stringify(await treeFiles()) === JSON.stringify(['lib/frozen.mydsl🔒', 'lib/util.mydsl']) && !onDisk('main.mydsl').includes('// x'), 'OK closes the files, the tree shows lib/ only, nothing was written: ' + JSON.stringify(await treeFiles()));
    await page.reload();
    await page.waitForSelector('#files-tree .file');
    check(await page.$eval('#project-folder', s => s.value) === 'lib' && JSON.stringify(await tabNames()) === '["Scratch*"]', 'the folder choice survives a reload');
    await page.selectOption('#project-folder', '');
    await page.waitForTimeout(250);
    // A conflict: the file changes on disk while a tab has unsaved edits; the reload flags it.
    await openFromTree('main.mydsl');
    await typeAtEnd('// mine\n');
    fs.writeFileSync(path.join(dir, 'main.mydsl'), 'Print("changed outside");\n');
    await page.reload();
    await page.waitForSelector('.cm-editor .cm-content');
    await page.waitForFunction(() => window.mydslDebug.tabs().length === 2);
    await page.waitForTimeout(300);
    check((await text()).endsWith('// mine\n') && (await fileStatus()).includes('changed on disk') && await page.$eval('#tabs .tab.active', e => e.classList.contains('conflict')), 'the unsaved text is kept and the disk change is flagged: ' + await fileStatus());
    await page.click('.cm-content'); await page.keyboard.press(`${mod}+s`);
    await page.waitForFunction(() => document.getElementById('file-status').textContent.startsWith('saved'));
    check(onDisk('main.mydsl').endsWith('// mine\n') && !(await page.$eval('#tabs .tab.active', e => e.classList.contains('conflict'))), 'saving overwrites the disk version and clears the flag');
    // ---- debugging across files: nested imports, errors and traces open the right file ----
    const marks = cls => page.$$eval(`.cm-mark-${cls}`, els => els.map(e => e.textContent));
    const cursor = () => page.evaluate(() => window.mydslDebug.editor().getCursor());
    const activePath = () => page.evaluate(() => window.mydslDebug.activeTab().path);
    while (await page.$('#tabs .tab .close')) { await page.hover('#tabs .tab:nth-child(2)'); await page.click('#tabs .tab:nth-child(2) .close'); await page.waitForTimeout(100); } // start clean
    await openFromTree('calc/main.mydsl');
    await page.waitForFunction(() => document.getElementById('status').textContent.startsWith('ok'));
    await page.click('#run');
    await page.waitForSelector('#output .runtime-error');
    const calcMain = 'import "ops.mydsl";\nPrint(ratio(10, 2));\nPrint(ratio(1, 0));\n';
    check((await page.textContent('#output')).startsWith("Int(500)\nRuntime error in 'calc/core.mydsl' at line 3, column 9: divide: b is zero (imported from 'calc/ops.mydsl' at line 1, column 1; imported from the main program at line 1, column 1)\n  ↳ in function 'divide' called from line 3, column 12 of 'calc/ops.mydsl'\n  ↳ in function 'ratio' called from line 3, column 7"),
          'a runtime error two imports deep: message with file and chain, frames with their files: ' + JSON.stringify(await page.textContent('#output')));
    check((await page.textContent('#run-file')) === 'calc/main.mydsl', 'the Output header names the file that was run');
    check(JSON.stringify(await marks('error')) === '["r"]' && JSON.stringify(await marks('trace')) === '["r"]' && await page.evaluate(() => window.mydslDebug.state().errorOffset) === calcMain.indexOf('ratio(1, 0)'), 'in the program the call site is marked (red and orange)');
    // The error line opens core.mydsl at the failing statement.
    await page.click('#output .runtime-error');
    await page.waitForFunction(() => window.mydslDebug.activeTab().path === 'calc/core.mydsl');
    await page.waitForTimeout(300);
    check(await cursor() === 'function int divide(int a, int b) {\n    if (b == 0) then {\n        '.length && (await page.textContent('#source-pos')) === 'line 3, column 9', 'clicking the error opens calc/core.mydsl at line 3, column 9: ' + await page.textContent('#source-pos'));
    check(JSON.stringify(await marks('error')) === '["e"]' && JSON.stringify(await marks('trace')) === '[]' && JSON.stringify(await tabNames()) === '["Scratch","main.mydsl","core.mydsl*"]', 'the error is marked in the library, no call-site marks there: ' + JSON.stringify(await marks('error')));
    check((await page.textContent('#status')).startsWith('ok ·'), 'the library parses on its own (AST/tokens show the library): ' + await page.textContent('#status'));
    // The frames: the one in ops.mydsl opens that file, the one in the program returns to it.
    await page.click('#output .trace-frame >> nth=0');
    await page.waitForFunction(() => window.mydslDebug.activeTab().path === 'calc/ops.mydsl');
    await page.waitForTimeout(300);
    check((await page.textContent('#source-pos')) === 'line 3, column 12' && JSON.stringify(await marks('trace')) === '["d"]' && JSON.stringify(await marks('error')) === '[]', 'the first frame opens calc/ops.mydsl at the call of divide, marked orange: ' + await page.textContent('#source-pos'));
    await page.click('#output .trace-frame >> nth=1');
    await page.waitForFunction(() => window.mydslDebug.activeTab().path === 'calc/main.mydsl');
    await page.waitForTimeout(200);
    check((await page.textContent('#source-pos')) === 'line 3, column 7' && JSON.stringify(await marks('error')) === '["r"]', 'the second frame returns to the program at the call of ratio');
    // A syntax error typed into a library (unsaved) is seen by the program's parse through the overlay; the banner opens the library.
    await page.click('#tabs .tab:nth-child(4)'); // ops
    await page.waitForTimeout(200);
    const opsText = await text();
    await typeAtEnd('int broken := ;');
    await page.waitForSelector('#error.show');
    check((await page.textContent('#error')).startsWith('Parse error at line 5'), 'the library tab shows its own parse error: ' + await page.textContent('#error'));
    await page.click('#tabs .tab:nth-child(2)'); // main
    await page.waitForFunction(() => document.getElementById('status').textContent === 'parse error');
    check((await page.textContent('#error')).startsWith("Parse error in 'calc/ops.mydsl' at line 5") && (await page.textContent('#error')).includes('imported from the main program at line 1, column 1'), 'the program\'s parse sees the unsaved, broken library: ' + await page.textContent('#error'));
    check(JSON.stringify(await marks('error')) === '["i"]', 'and marks the import statement');
    await page.click('#error');
    await page.waitForFunction(() => window.mydslDebug.activeTab().path === 'calc/ops.mydsl');
    await page.waitForTimeout(200);
    check((await page.textContent('#source-pos')).startsWith('line 5, column '), 'clicking the banner opens the library at the error: ' + await page.textContent('#source-pos'));
    await page.click('.cm-content');
    for (let i = 0; i < 5 && (await text()) !== opsText; i++) await page.keyboard.press(`${mod}+z`); // the library is whole again (the file on disk never changed)
    await page.waitForTimeout(300);
    check(await text() === opsText, 'undo restored the library text');
    check(onDisk('calc/ops.mydsl') === 'import "core.mydsl";\nfunction int ratio(int a, int b) {\n    return divide(a, b) * 100;\n};\n', 'the disk was never touched');
    // Unsaved edits in a library are what Run uses; the status says so; the disk is untouched.
    await page.evaluate(() => { const ed = window.mydslDebug.editor(); ed.setCursor(ed.getValue().indexOf('100') + 3); ed.focus(); });
    await page.keyboard.type('0');
    await page.waitForTimeout(300);
    check((await text()).includes('* 1000'), 'edited ops.mydsl to * 1000 (unsaved)');
    await page.click('#tabs .tab:nth-child(2)'); // main
    await page.waitForFunction(() => document.getElementById('status').textContent.startsWith('ok'));
    await page.click('#run');
    await page.waitForFunction(() => document.getElementById('output').textContent.startsWith('Int(5000)'));
    check((await page.textContent('#output')).startsWith('Int(5000)\n') && (await page.textContent('#run-status')).includes('with the unsaved text of calc/ops.mydsl') && onDisk('calc/ops.mydsl').includes('* 100;'), 'Run uses the unsaved library text and says so; nothing was saved: ' + await page.textContent('#run-status'));
    // Run always runs the active tab: running the library itself produces no output; the header says which file.
    await page.click('#tabs .tab:nth-child(3)'); // core
    await page.waitForTimeout(200);
    await page.click('#run');
    await page.waitForFunction(() => document.getElementById('run-file').textContent === 'calc/core.mydsl');
    await page.waitForTimeout(200);
    check((await page.textContent('#output')).includes('produced no output') && (await page.textContent('#run-status')).startsWith('finished'), 'running the library tab runs that file (only definitions, no output)');
    // A single-file program in the Scratch tab behaves as before: the error is in the program itself.
    await page.click('#tabs .tab:nth-child(1)');
    await page.waitForTimeout(200);
    await setText('int a := 2;\nPrint(a / 0);\n');
    await page.click('#run');
    await page.waitForFunction(() => document.getElementById('output').textContent.startsWith('Runtime error at line 2'));
    check((await page.textContent('#run-file')) === 'Scratch' && (await page.textContent('#output')) === 'Runtime error at line 2, column 9: Division by zero: 2 / 0' && JSON.stringify(await marks('error')) === '["/"]', 'a single-file program: plain message, mark on the operator');
    await page.click('#tabs .tab:nth-child(2)');
    await page.waitForTimeout(200);
    check(JSON.stringify(await marks('error')) === '[]' && await page.$eval('#output-pane', p => p.classList.contains('stale')), 'another tab shows no marks of that run and the output is flagged as belonging elsewhere');
    await page.click('#output .runtime-error');
    await page.waitForTimeout(200);
    check(await activePath() === null && await cursor() === 'int a := 2;\nPrint(a '.length, 'clicking the error returns to the Scratch tab at the position');
    while (await page.$('#tabs .tab .close')) { await page.hover('#tabs .tab:nth-child(2)'); await page.click('#tabs .tab:nth-child(2) .close'); await page.waitForTimeout(100); }
    // The file API itself refuses anything outside the project and other file kinds (the page never asks for those, but others might).
    const status = async (method, target, body) => page.evaluate(async ({ method, target, body }) => { const r = await fetch(target, { method, body, headers: { 'X-Requested-With': 'mydsl' } }); return [r.status, (await r.json()).error || 'ok']; }, { method, target, body });
    check(JSON.stringify(await status('GET', '/api/file?path=../' + path.basename(outside))) === JSON.stringify([400, "The path must not contain empty, '.' or '..' components"]), 'no ".." paths');
    check(JSON.stringify(await status('GET', '/api/file?path=lib/escape.mydsl')).startsWith('[403,"The path leaves the project directory'), 'a symbolic link leading outside is refused: ' + JSON.stringify(await status('GET', '/api/file?path=lib/escape.mydsl')));
    check(JSON.stringify(await status('GET', '/api/file?path=notes.md')).startsWith('[403,"Only .mydsl and .txt'), 'other extensions are refused');
    check(JSON.stringify(await status('GET', '/api/file?path=.secret.mydsl')).startsWith('[403,"Hidden entries') && JSON.stringify(await status('GET', '/api/file?path=build/generated.mydsl')).startsWith('[403,"Hidden entries'), 'hidden files and build/ are refused by path too');
    check(JSON.stringify(await status('PUT', '/api/file?path=lib/escape.mydsl', 'x')).startsWith('[403') && fs.readFileSync(outside, 'utf8') === 'Print("outside");\n', 'writing through the link is refused and the outside file is untouched');
    check(JSON.stringify(await status('PUT', '/api/file?path=lib/nested/new.mydsl', 'x')).startsWith('[404,"No such directory'), 'no directories are created');
    check(JSON.stringify(await status('GET', '/api/file?path=' + encodeURIComponent('/etc/passwd'))) === JSON.stringify([400, 'The path must be relative to the project directory, not absolute']), 'no absolute paths');
    check(problems.length === 0, 'no page errors or console errors in the file checks', problems.join('\n      '));
  } finally {
    server.child.kill();
    await page.close();
    try { fs.chmodSync(path.join(dir, 'lib', 'frozen.mydsl'), 0o644); } catch (e) { /* already gone */ }
    fs.rmSync(dir, { recursive: true, force: true });
    fs.rmSync(outside, { force: true });
  }
}

(async () => {
  const browser = await playwright.chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
  const problems = [];
  page.on('pageerror', e => problems.push(`page error: ${e.message}`));
  page.on('console', m => { if (m.type() === 'error' || m.type() === 'warning') problems.push(`console ${m.type()}: ${m.text()}`); });
  page.on('requestfailed', r => problems.push(`request failed: ${r.url()}`));
  await page.goto(url);
  await page.waitForSelector('.cm-editor .cm-content');
  await page.evaluate(() => localStorage.clear());

  // Helpers: the page exposes window.mydslDebug (state, editor), the editor exposes its EditorView.
  const text = () => page.evaluate(() => window.mydslDebug.editor().getValue());
  const cursor = () => page.evaluate(() => window.mydslDebug.editor().getCursor());
  const setText = async t => { await page.evaluate(t => window.mydslDebug.editor().setValue(t), t); await page.waitForTimeout(350); };
  const marks = cls => page.$$eval(`.cm-mark-${cls}`, els => els.map(e => e.textContent));
  const tokensOf = cls => page.$$eval(`.cm-content .${cls}`, els => els.map(e => e.textContent));
  const classOfSpan = async needle => page.evaluate(needle => {
    const span = [...document.querySelectorAll('.cm-content span')].find(s => s.textContent === needle);
    return span ? span.className : null;
  }, needle);
  const focusAt = async offset => { await page.evaluate(o => { const ed = window.mydslDebug.editor(); ed.setCursor(o); ed.focus(); }, offset); };

  // ---- loads, highlights, line numbers ------------------------------------------------------
  const program = '// comment\nint n := 5;\nstring s := "hi\\n";\nfloat f := 2.5e3;\nif (n > 1) then {\n    Print(abs(f));\n};\n';
  await setText(program);
  check(await text() === program, 'setValue puts the program into the editor');
  const gutter = await page.$$eval('.cm-lineNumbers .cm-gutterElement', els => els.filter(e => getComputedStyle(e).visibility !== 'hidden').map(e => e.textContent).filter(t => /^\d+$/.test(t)));
  check(gutter.length === 8 && gutter[0] === '1' && gutter[7] === '8', 'line numbers 1-8 in the gutter: ' + gutter.join(','));
  // Every token class below comes from the MyDSL stream parser: different colours per kind.
  const cls = {};
  for (const word of ['// comment', 'int', 'string', 'float', 'if', 'then', 'Print', 'abs', '"hi\\n"', '5', '2.5e3', ':=', '>', 'n'])
    cls[word] = await classOfSpan(word);
  check(cls['// comment'] && /ͼ/.test(cls['// comment']), 'comments are highlighted: ' + cls['// comment']);
  check(cls['int'] && cls['int'] === cls['string'] && cls['int'] === cls['float'], 'type keywords share one style');
  check(cls['if'] && cls['if'] === cls['then'] && cls['if'] === cls['Print'] && cls['if'] !== cls['int'], 'keywords share one style, different from types');
  check(cls['"hi\\n"'] && cls['"hi\\n"'] !== cls['int'] && cls['"hi\\n"'] !== cls['// comment'], 'strings have their own style');
  check(cls['5'] && cls['5'] === cls['2.5e3'] && cls['5'] !== cls['"hi\\n"'], 'integers and floats share the number style');
  check(cls['abs'] && cls['abs'] !== cls['if'], 'builtins are styled differently from keywords');
  check(cls[':='] && cls[':='] === cls['>'], 'operators are styled');
  check(cls['n'] === null || !cls['n'] || cls['n'] !== cls['if'], 'plain variables are not keyword-coloured');
  const highlightedWords = await page.$$eval('.cm-content span[class*="ͼ"]', els => els.length);
  check(highlightedWords >= 12, 'many highlighted spans: ' + highlightedWords);
  // An unterminated string gets the invalid style (a lexing error in C++; the red error mark
  // of that error splits off the opening quote).
  await setText('string s := "open;\n');
  await page.waitForSelector('#error.show');
  check(await classOfSpan('open;') && (await classOfSpan('open;')) !== cls['"hi\\n"'] && (await classOfSpan('open;')) !== cls['n'], 'an unterminated string is marked invalid: ' + await classOfSpan('open;'));
  check(JSON.stringify(await marks('error')) === JSON.stringify(['"']), 'and the lexing error mark sits on the opening quote');

  // ---- typing, auto-indentation, bracket auto-closing -------------------------------------
  // Typing "{" inserts "}" after the cursor; Enter between them opens an indented line.
  await setText('');
  await page.click('.cm-content');
  await page.keyboard.type('while (i < 3) {');
  check(await text() === 'while (i < 3) {}', 'typing "{" inserts the closing "}"');
  await page.keyboard.press('Enter');
  await page.keyboard.type('i := i + 1;');
  await page.waitForTimeout(50);
  check(await text() === 'while (i < 3) {\n    i := i + 1;\n}', 'Enter between the braces indents the body by 4 and keeps "}" on its own line: ' + JSON.stringify(await text()));
  await page.keyboard.press('ArrowDown');
  await page.keyboard.press('End');
  await page.keyboard.type(';');
  await page.keyboard.press('Enter');
  await page.keyboard.type('Print(i);');
  await page.waitForTimeout(300);
  check(await text() === 'while (i < 3) {\n    i := i + 1;\n};\nPrint(i);', 'after the block the next line starts at column 1: ' + JSON.stringify(await text()));
  // Without auto-closing (pasted text or deleted "}"): typing "}" on an indented line outdents it.
  await setText('if (x) then {\n    Print(x);\n');
  await focusAt((await text()).length);
  await page.keyboard.press('Enter'); // the new line is indented by 4 (still inside the block)
  check(await text() === 'if (x) then {\n    Print(x);\n\n    ', 'Enter inside an open block indents by 4: ' + JSON.stringify(await text()));
  await page.keyboard.type('}');
  check(await text() === 'if (x) then {\n    Print(x);\n\n}', 'typing "}" moves it back to column 1: ' + JSON.stringify(await text()));
  await setText('');
  await page.click('.cm-content');
  await page.keyboard.type('Print(');
  check(await text() === 'Print()', 'typing "(" inserts the closing ")"');
  await page.keyboard.type('a');
  await page.keyboard.type(')'); // typing the closer steps over the inserted one
  check(await text() === 'Print(a)', 'typing ")" over the auto-inserted one does not double it: ' + JSON.stringify(await text()));
  await page.keyboard.type(';');
  await page.keyboard.press('Enter');
  await page.keyboard.type('v := [');
  check(await text() === 'Print(a);\nv := []', '"[" is auto-closed');
  await page.keyboard.type('1, 2');
  await page.keyboard.press('End');
  await page.keyboard.type(';');
  await page.keyboard.press('Enter');
  await page.keyboard.type('s := "');
  check((await text()).endsWith('s := ""'), '"\\"" is auto-closed');
  // Nested blocks: 4, 8, back to 4, back to 0 (the closing braces come from auto-closing).
  await setText('');
  await page.click('.cm-content');
  await page.keyboard.type('function int f(int x) {');
  await page.keyboard.press('Enter');
  await page.keyboard.type('if (x > 0) then {');
  await page.keyboard.press('Enter');
  await page.keyboard.type('return 1;');
  await page.keyboard.press('ArrowDown'); await page.keyboard.press('End'); await page.keyboard.type(';');
  await page.keyboard.press('Enter');
  await page.keyboard.type('return 0;');
  await page.keyboard.press('ArrowDown'); await page.keyboard.press('End'); await page.keyboard.type(';');
  await page.waitForTimeout(50);
  check(await text() === 'function int f(int x) {\n    if (x > 0) then {\n        return 1;\n    };\n    return 0;\n};', 'nested blocks indent 4, 8, 4, 0: ' + JSON.stringify(await text()));
  // Tab indents the line, Shift+Tab outdents it; the focus stays in the editor.
  await page.keyboard.press('Home');
  await page.keyboard.press('Tab');
  check((await text()).endsWith('\n    };'), 'Tab indents the current line');
  await page.keyboard.press('Shift+Tab');
  check((await text()).endsWith('\n};'), 'Shift+Tab outdents it');
  check(await page.evaluate(() => document.activeElement && document.activeElement.classList.contains('cm-content')), 'the focus stays in the editor after Tab');

  // ---- bracket matching --------------------------------------------------------------------
  const brackets = 'Print((1 + 2) * [3, 4][0]);\nint x := (1;\n';
  await setText(brackets);
  await focusAt(brackets.indexOf('(1 + 2)'));  // cursor before the "(" of (1 + 2)
  await page.waitForTimeout(50);
  let matching = await page.$$eval('.cm-matchingBracket', els => els.map(e => e.textContent));
  check(matching.join('') === '()', 'the cursor before "(" highlights it and its ")": ' + JSON.stringify(matching));
  await focusAt(brackets.indexOf('[0]') + 3); // cursor after "]" of [0]
  await page.waitForTimeout(50);
  matching = await page.$$eval('.cm-matchingBracket', els => els.map(e => e.textContent));
  check(matching.join('') === '[]', 'the cursor after "]" highlights the pair: ' + JSON.stringify(matching));
  await focusAt(brackets.indexOf('(1;') + 1); // after the unmatched "("
  await page.waitForTimeout(50);
  const bad = await page.$$eval('.cm-nonmatchingBracket', els => els.map(e => e.textContent));
  check(bad.join('') === '(', 'an unmatched "(" is flagged: ' + JSON.stringify(bad));

  // ---- undo / redo -------------------------------------------------------------------------
  await setText('int a := 1;\n');
  await focusAt((await text()).length);
  await page.keyboard.type('Print(a);');
  await page.waitForTimeout(600); // let the history group close
  await page.keyboard.type('\nPrint(2);');
  check(await text() === 'int a := 1;\nPrint(a);\nPrint(2);', 'typed two statements');
  await page.keyboard.press(`${mod}+z`);
  check(await text() === 'int a := 1;\nPrint(a);', 'Ctrl+Z undoes the last statement: ' + JSON.stringify(await text()));
  await page.keyboard.press(`${mod}+z`);
  check(await text() === 'int a := 1;\n', 'Ctrl+Z again undoes the first one');
  await page.keyboard.press(`${mod}+Shift+Z`); // (a browser reports key "Z" for Shift+z; Playwright needs it spelled so)
  check(await text() === 'int a := 1;\nPrint(a);', 'Ctrl+Shift+Z redoes: ' + JSON.stringify(await text()));
  await page.keyboard.press(process.platform === 'darwin' ? 'Meta+Shift+Z' : 'Control+y'); // Ctrl+Y is the Windows/Linux redo; macOS only has Cmd+Shift+Z
  check(await text() === 'int a := 1;\nPrint(a);\nPrint(2);', 'Ctrl+Y (Cmd+Shift+Z on macOS) redoes too');
  await page.keyboard.press(`${mod}+z`);
  await page.keyboard.press(`${mod}+z`);
  await page.waitForTimeout(350);
  check((await page.textContent('#status')).startsWith('ok · 6 tokens · 1 statements'), 'the parser followed the undo: ' + await page.textContent('#status'));

  // ---- line operations and search panel -----------------------------------------------------
  await setText('int a := 1;\nint b := 2;\n');
  await focusAt(0);
  await page.keyboard.press('Alt+ArrowDown');
  check(await text() === 'int b := 2;\nint a := 1;\n', 'Alt+Down moves the line down');
  await page.keyboard.press(`${mod}+/`);
  check(await text() === 'int b := 2;\n// int a := 1;\n', 'Ctrl+/ comments the line with //: ' + JSON.stringify(await text()));
  await page.keyboard.press(`${mod}+/`);
  check(await text() === 'int b := 2;\nint a := 1;\n', 'Ctrl+/ again uncomments');
  await page.keyboard.press(`${mod}+f`);
  check(await page.$('.cm-search') !== null, 'Ctrl+F opens the find/replace panel');
  await page.keyboard.press('Escape');
  check(await page.$('.cm-search') === null, 'Escape closes it');

  // ---- the page's features through the editor -------------------------------------------
  // Parse errors: banner, red mark on the offending token, the token row flagged; click jumps.
  await setText('int a := 1;\nint b := (a +\n  2;\n');
  await page.waitForSelector('#error.show');
  check((await page.textContent('#error')).startsWith('Parse error at line 3, column 4'), 'parse error banner: ' + await page.textContent('#error'));
  check(JSON.stringify(await marks('error')) === '[";"]', 'the error mark sits on the ";": ' + JSON.stringify(await marks('error')));
  await page.click('#error');
  check(await cursor() === 'int a := 1;\nint b := (a +\n  2'.length, 'clicking the banner moves the cursor to the error: ' + await cursor());
  check(await page.evaluate(() => document.activeElement.classList.contains('cm-content')), 'and focuses the editor');
  check(await page.textContent('#source-pos') === 'line 3, column 4', 'cursor position display: ' + await page.textContent('#source-pos'));
  // An error at the very end of the input is drawn as a box after the text.
  await setText('int a := 1');
  await page.waitForSelector('#error.show');
  check(await page.$('.cm-mark-error.cm-mark-end') !== null, 'end-of-input error shows a marker after the text');
  // Type errors keep the AST and outline the node; tokens/AST clicks mark the editor text.
  await setText('int a := 1;\nfloat b := 2.0;\nPrint(a + b);\n');
  await page.waitForSelector('#error.show');
  check(JSON.stringify(await marks('error')) === '["+"]', 'type error mark on the operator');
  check(await page.$('#ast .node.error-node') !== null, 'the AST node is outlined');
  await page.click('#toggle-tokens');
  await page.click('#tokens tr:nth-child(2)'); // token "a"
  check(JSON.stringify(await marks('selected')) === '["a"]', 'clicking a token marks it in the editor: ' + JSON.stringify(await marks('selected')));
  await page.click('#ast .node >> nth=2'); // the second declaration
  check(JSON.stringify(await marks('selected')) === '["float b := 2.0;"]', 'clicking an AST node marks its whole span: ' + JSON.stringify(await marks('selected')));
  // Moving the cursor in the editor tracks the token and the innermost node.
  await focusAt('int a := 1;\nfloat b := 2.0;\nPrint(a + '.length); // on "b" of the last line
  await page.waitForTimeout(50);
  check(await page.$eval('#tokens tr.at-cursor td:nth-child(3)', td => td.textContent) === 'b', 'the token under the cursor is marked');
  check((await page.$eval('#ast .node.at-cursor', n => n.textContent)).includes('variable b'), 'the innermost node under the cursor is marked');
  // Editing clears the selection mark and after 200 ms re-parses.
  await focusAt('int a := 1;\nfloat b := 2.0;\nPrint(a + b);\n'.length);
  await page.keyboard.type('Print(b);');
  check(JSON.stringify(await marks('selected')) === '[]', 'typing drops the selection mark');
  await page.waitForTimeout(400);
  check((await page.textContent('#token-count')) === '23 tokens', 're-parsed after typing: ' + await page.textContent('#token-count'));
  // Run: output, runtime error with the trace, red/orange marks, click-to-jump, variables.
  const crash = 'function int half(int x) { return 10 / x; };\nint a := half(2);\nPrint(a);\nPrint(half(0));\n';
  await setText(crash);
  await page.click('#run');
  await page.waitForSelector('#output .runtime-error');
  check((await page.textContent('#output')).startsWith('Int(5)\nRuntime error at line 1, column 38: Division by zero: 10 / 0'), 'Run output: ' + await page.textContent('#output'));
  check(JSON.stringify(await marks('error')) === '["/"]' && JSON.stringify(await marks('trace')) === '["h"]', 'runtime error (red) and call site (orange) marks: ' + JSON.stringify([await marks('error'), await marks('trace')]));
  await page.click('#output .trace-frame');
  check(await cursor() === crash.indexOf('half(0)'), 'clicking the trace frame jumps to the call site');
  check(await page.$eval('#variables tr td.value', td => td.textContent) === 'Int(5)', 'the variable inspector shows a = Int(5)');
  await page.click('#variables tr');
  check(JSON.stringify(await marks('selected')) === '["int a := half(2);"]', 'clicking a variable marks its declaration');
  await focusAt(0);
  await page.keyboard.type('x');
  check(await page.$eval('#output-pane', p => p.classList.contains('stale')), 'typing after a run marks the output as stale');
  await page.keyboard.press(`${mod}+z`);
  // Ctrl+Enter parses, Ctrl+Shift+Enter runs, from inside the editor.
  await setText('Print(1 + 2);');
  await page.click('.cm-content');
  await page.keyboard.press(`${mod}+Enter`);
  await page.waitForTimeout(200);
  check((await page.textContent('#status')).startsWith('ok · 8 tokens'), 'Ctrl+Enter parses: ' + await page.textContent('#status'));
  check(await text() === 'Print(1 + 2);', 'Ctrl+Enter does not insert a line');
  await page.keyboard.press(`${mod}+Shift+Enter`);
  await page.waitForFunction(() => document.getElementById('run-status').textContent.startsWith('finished'));
  check((await page.textContent('#output')) === 'Int(3)\n', 'Ctrl+Shift+Enter runs: ' + JSON.stringify(await page.textContent('#output')));
  // The same shortcuts work with the focus outside the editor (e.g. in the Input box); the other modifier works too.
  await setText('Print(4);');
  await page.click('#stdin');
  await page.keyboard.press(process.platform === 'darwin' ? 'Control+Shift+Enter' : 'Meta+Shift+Enter');
  await page.waitForFunction(() => document.getElementById('output').textContent === 'Int(4)\n');
  check((await page.textContent('#output')) === 'Int(4)\n', 'Shift+Enter with the other modifier, from the Input box, runs too');
  await page.click('.cm-content');
  await page.keyboard.press(process.platform === 'darwin' ? 'Control+Enter' : 'Meta+Enter');
  await page.waitForTimeout(200);
  check(await text() === 'Print(4);' && (await page.textContent('#status')).startsWith('ok · 6 tokens'), 'the other modifier + Enter parses inside the editor without inserting a line');
  // Examples load into the editor; the text survives a reload.
  await page.selectOption('#examples', 'factorial.txt');
  await page.waitForTimeout(400);
  check((await text()).includes('while (i <= num)'), 'loading an example replaces the text');
  await page.keyboard.press(`${mod}+z`);
  check(await text() === 'Print(4);', 'loading an example can be undone');
  await setText('int persisted := 42;\n');
  await page.click('#parse');
  await page.waitForTimeout(200);
  await page.reload();
  await page.waitForSelector('.cm-editor .cm-content');
  check(await text() === 'int persisted := 42;\n', 'the text is restored after a reload');
  // The editor fills its pane and resizes with it.
  const before = await page.$eval('.cm-editor', e => e.getBoundingClientRect().height);
  await page.click('#hide-output');
  const after = await page.$eval('.cm-editor', e => e.getBoundingClientRect().height);
  check(after > before + 100, `hiding the output panel grows the editor (${before} -> ${after})`);
  check(await page.$('.editor .fallback') === null, 'the fallback notice is removed once the editor is up');

  // ---- completion ---------------------------------------------------------------------------
  const listItems = () => page.$$eval('.cm-tooltip-autocomplete li', els => els.map(e => e.querySelector('.cm-completionLabel').textContent));
  const listDetails = () => page.$$eval('.cm-tooltip-autocomplete li', els => els.map(e => e.textContent));
  const selectedItem = () => page.$eval('.cm-tooltip-autocomplete li[aria-selected]', e => e.querySelector('.cm-completionLabel').textContent).catch(() => null);
  const listOpen = () => page.$('.cm-tooltip-autocomplete').then(e => e !== null);
  // The list ignores accept/move keys for 75 ms after opening (CodeMirror's interactionDelay,
  // against accidental accepts), so wait a little longer than that before pressing keys.
  const waitList = async () => { await page.waitForSelector('.cm-tooltip-autocomplete', { timeout: 3000 }); await page.waitForTimeout(120); };
  const typeAtEnd = async s => { await page.evaluate(() => { const ed = window.mydslDebug.editor(); ed.setCursor(ed.getValue().length); ed.focus(); }); await page.keyboard.type(s); };
  const program2 = 'import "stdlib/math.mydsl";\nfunction int scale(int value, int factor) {\n    return value * factor;\n};\nint total := 0;\nfloatvector 3 vec := [1.0, 2.0, 3.0];\nfor (counter := 0, counter < 3, 1) {\n    total := total + counter;\n};\n';
  await setText(program2);
  await page.waitForFunction(() => document.getElementById('status').textContent.startsWith('ok'));
  // Typing a name opens the list; it holds every kind of symbol.
  await typeAtEnd('Print(s');
  await waitList();
  let items = await listItems();
  check(items.includes('scale') && items.includes('sign') && items.includes('square') && items.includes('substring') && items.includes('string'),
        'typing "s" suggests the own function, imported functions, a builtin and a type: ' + items.join(','));
  check((await listDetails()).some(d => /scale\s*\(int value, int factor\) → int/.test(d)), 'a function shows its parameters and return type: ' + (await listDetails()).join(' | '));
  check((await listDetails()).some(d => /substring\s*\(s, start, count\)/.test(d)), 'a builtin shows its parameters');
  check(await selectedItem() === 'scale', 'the first option is selected (own symbols rank first): ' + await selectedItem());
  await page.waitForSelector('.cm-completionInfo');
  const info = await page.$eval('.cm-completionInfo', e => e.textContent);
  check(info.includes('scale(int value, int factor) → int') && info.includes('Function defined at line 2'), 'the info box shows the signature and where the function is defined: ' + info);
  // Arrow keys move, Enter accepts (with parameter placeholders), the list closes.
  await page.keyboard.press('ArrowDown');
  check(await selectedItem() === 'sign', 'ArrowDown selects the next option: ' + await selectedItem());
  check((await page.$eval('.cm-completionInfo', e => e.textContent)).includes("stdlib/math.mydsl"), 'the info of an imported function names its module: ' + await page.$eval('.cm-completionInfo', e => e.textContent));
  await page.keyboard.press('ArrowDown'); await page.keyboard.press('ArrowUp');
  check(await selectedItem() === 'sign', 'ArrowUp goes back');
  await page.keyboard.press('Enter');
  await page.waitForTimeout(100);
  check((await text()).endsWith('Print(sign(x))') && !(await listOpen()), 'Enter inserts the function with a placeholder for its parameter and closes the list: ' + JSON.stringify((await text()).slice(-20)));
  await page.keyboard.type('total'); // replaces the selected placeholder
  check((await text()).endsWith('Print(sign(total))'), 'typing replaces the placeholder: ' + JSON.stringify((await text()).slice(-20)));
  await page.keyboard.press('Escape'); // leaves the snippet fields (a completion list may be open for "total")
  // Tab accepts too; variables, loop variables and parameters are suggested with their types.
  await setText(program2);
  await page.waitForFunction(() => document.getElementById('status').textContent.startsWith('ok'));
  await typeAtEnd('Print(to');
  await waitList();
  items = await listItems();
  check(items[0] === 'total' && items.includes('to_float') && items.includes('to_int') && items.includes('to_string'), '"to" suggests the variable first, then the builtins: ' + items.join(','));
  check((await listDetails())[0].includes('total') && (await listDetails())[0].includes('int'), 'a variable shows its type');
  await page.keyboard.press('Tab');
  check((await text()).endsWith('Print(total)') && !(await listOpen()), 'Tab accepts the selected option: ' + JSON.stringify((await text()).slice(-12)));
  await page.keyboard.type(');');
  await page.keyboard.press('Enter');
  await page.keyboard.type('Print(c');
  await waitList();
  items = await listItems();
  check(items.includes('counter'), 'the for loop variable is suggested: ' + items.join(','));
  await page.keyboard.press('Escape');
  check(!(await listOpen()), 'Escape closes the list');
  await page.keyboard.type('ounter);');
  await page.keyboard.press('Enter');
  await page.keyboard.type('Print(scale(va');
  await waitList();
  items = await listItems();
  check(items[0] === 'value' && (await listDetails())[0].includes('int'), 'a parameter is suggested with its type: ' + (await listDetails()).join(','));
  await page.keyboard.press('Escape');
  await page.keyboard.type('lue, fa');
  await waitList();
  check((await listItems()).includes('factor') && (await listItems()).includes('false') && (await listItems()).includes('factorial'), 'parameter, constant and imported function for "fa": ' + (await listItems()).join(','));
  await page.keyboard.press('Escape');
  // Types and keywords with their descriptions; a statement keyword inserts a template.
  await setText('');
  await page.click('.cm-content');
  await page.keyboard.type('floatm');
  await waitList();
  check((await listItems())[0] === 'floatmatrix' && (await page.$eval('.cm-completionInfo', e => e.textContent)).includes('floatmatrix R,C name := [[1.0, 2.0], [3.0, 4.0]];'), 'a type shows its declaration form: ' + await page.$eval('.cm-completionInfo', e => e.textContent));
  await page.keyboard.press('Enter');
  check(await text() === 'floatmatrix', 'accepting a type inserts the word only');
  await setText('');
  await page.click('.cm-content');
  await page.keyboard.type('whi');
  await waitList();
  check((await listItems())[0] === 'while' && (await page.$eval('.cm-completionInfo', e => e.textContent)).includes('Loop while the bool condition holds'), 'the while keyword shows its description');
  await page.keyboard.press('Enter');
  await page.waitForTimeout(100);
  check(await text() === 'while (condition) {\n    \n};', 'accepting "while" inserts the statement template: ' + JSON.stringify(await text()));
  await page.keyboard.type('n > 0'); // the first field (condition) is selected
  await page.keyboard.press('Tab');   // the body field
  await page.keyboard.type('n := n - 1;');
  check(await text() === 'while (n > 0) {\n    n := n - 1;\n};', 'typing fills the condition, Tab jumps to the body: ' + JSON.stringify(await text()));
  await page.keyboard.press('Escape');
  await setText('');
  await page.click('.cm-content');
  await page.keyboard.type('func');
  await waitList();
  await page.keyboard.press('Enter');
  await page.waitForTimeout(100);
  check((await text()).startsWith('function int name(int x) {\n    \n    return value;\n};'), 'the function template: ' + JSON.stringify(await text()));
  await page.keyboard.press('Escape');
  await setText('');
  await page.click('.cm-content');
  await page.keyboard.type('Pri');
  await waitList();
  check((await listItems())[0] === 'Print', '"Pri" suggests Print');
  await page.keyboard.press('Enter');
  await page.waitForTimeout(100);
  check(await text() === 'Print(value);', 'accepting Print inserts Print(value); with the value selected: ' + JSON.stringify(await text()));
  await page.keyboard.type('42');
  check(await text() === 'Print(42);', 'typing replaces the value: ' + JSON.stringify(await text()));
  await page.keyboard.press('Escape');
  // Builtins complete with placeholders for their arguments.
  await setText('');
  await page.click('.cm-content');
  await page.keyboard.type('string t := subs');
  await waitList();
  check((await page.$eval('.cm-completionInfo', e => e.textContent)).includes('string, int, int -> string') && (await page.$eval('.cm-completionInfo', e => e.textContent)).includes('count characters of s'), 'a builtin shows its type rule and description: ' + await page.$eval('.cm-completionInfo', e => e.textContent));
  await page.keyboard.press('Enter');
  check(await text() === 'string t := substring(s, start, count)', 'a builtin is inserted with placeholders: ' + JSON.stringify(await text()));
  await page.keyboard.press('Escape');
  // Incomplete code: names come from the lexer's words and the last parse that had an AST.
  await setText('int alpha := 1;\nint beta := (alpha +\nPrint(al');
  await page.waitForSelector('#error.show');
  await page.evaluate(() => { const ed = window.mydslDebug.editor(); ed.setCursor(ed.getValue().length); ed.focus(); });
  await page.keyboard.type('p');
  await waitList();
  check((await listItems()).includes('alpha'), 'while the program does not parse, names of the text are still suggested: ' + (await listItems()).join(','));
  await page.keyboard.press('Escape');
  await focusAt((await text()).length);
  await page.keyboard.type('); Print(be');
  await waitList();
  check((await listItems()).includes('beta') && !(await listItems()).includes('be'), 'the word being typed is not suggested for itself: ' + (await listItems()).join(','));
  await page.keyboard.press('Escape');
  // No suggestions inside strings and comments; Ctrl+Space opens the list explicitly.
  await setText('');
  await page.click('.cm-content');
  await page.keyboard.type('Print("wh');
  await page.waitForTimeout(300);
  check(!(await listOpen()), 'no completion inside a string literal');
  await setText('');
  await page.click('.cm-content');
  await page.keyboard.type('// wh');
  await page.waitForTimeout(300);
  check(!(await listOpen()), 'no completion inside a comment');
  await setText('');
  await page.click('.cm-content');
  await page.keyboard.type('int a := 1;');
  await page.keyboard.press('Enter');
  await page.waitForTimeout(300);
  check(!(await listOpen()), 'nothing pops up after punctuation or on a blank line');
  await page.keyboard.press('Control+Space');
  await waitList();
  check((await listItems()).length > 20 && (await listItems()).includes('a') && (await listItems()).includes('while'), 'Ctrl+Space lists everything: ' + (await listItems()).length);
  await page.keyboard.press('Escape');
  // Normal typing is not disturbed: a complete statement typed with Enter at the end stays as typed.
  await setText('');
  await page.click('.cm-content');
  await page.keyboard.type('int count := 2;');
  await page.keyboard.press('Enter');
  await page.keyboard.type('Print(count * 2);');
  await page.keyboard.press('Enter');
  await page.waitForTimeout(350);
  check(await text() === 'int count := 2;\nPrint(count * 2);\n', 'typing whole statements is unchanged by the completion: ' + JSON.stringify(await text()));
  check((await page.textContent('#status')).startsWith('ok · 13 tokens'), 'and the program parses: ' + await page.textContent('#status'));
  await page.click('#run');
  await page.waitForFunction(() => document.getElementById('run-status').textContent.startsWith('finished'));
  check((await page.textContent('#output')) === 'Int(4)\n', 'Run still works');

  // ---- themes ---------------------------------------------------------------------------------
  const rgb = hex => { const m = /^#(..)(..)(..)$/.exec(hex); return `rgb(${parseInt(m[1], 16)}, ${parseInt(m[2], 16)}, ${parseInt(m[3], 16)})`; };
  const editorBg = () => page.$eval('.cm-editor', e => getComputedStyle(e).backgroundColor);
  const statusBg = () => page.$eval('#statusbar', e => getComputedStyle(e).backgroundColor);
  const keywordColor = () => page.evaluate(() => { const span = [...document.querySelectorAll('.cm-content span')].find(s => s.textContent === 'while'); return span && getComputedStyle(span).color; });
  await setText('while (true) {\n    Print(1);\n};\n');
  const themeIds = await page.$$eval('#theme option', os => os.map(o => o.value));
  check(themeIds[0] === 'auto' && themeIds.length >= 13 && themeIds.includes('vs-dark') && themeIds.includes('monokai') && themeIds.includes('solarized-light'), 'the theme selector offers Auto and the themes: ' + themeIds.join(','));
  check(await editorBg() === rgb('#ffffff') && await statusBg() === rgb('#2563eb') && await keywordColor() === rgb('#7c3aed'), 'default (auto, light system): white editor, blue status bar, purple keywords');
  await page.selectOption('#theme', 'vs-dark');
  await page.waitForTimeout(150);
  check(await editorBg() === rgb('#1e1e1e') && await statusBg() === rgb('#007acc') && await keywordColor() === rgb('#c586c0'), 'Dark+: dark editor, VS Code blue status bar, pink keywords: ' + [await editorBg(), await statusBg(), await keywordColor()].join(' '));
  check(await page.$eval('body', b => getComputedStyle(b).backgroundColor) === rgb('#252526') && await page.$eval('#files-pane', p => getComputedStyle(p).backgroundColor) === rgb('#252526') && await page.$eval('#tabs', p => getComputedStyle(p).backgroundColor) === rgb('#252526'),
        'the chrome follows: workbench, file panel and tab strip');
  check(await page.$eval('#run', b => getComputedStyle(b).backgroundColor) === rgb('#4ec94e') && await page.$eval('#run', b => getComputedStyle(b).color) === rgb('#000000'), 'the Run button uses the theme\'s ok colour with readable text');
  // The theme survives a reload.
  await page.reload();
  await page.waitForSelector('.cm-editor .cm-content');
  await page.waitForTimeout(200);
  check(await editorBg() === rgb('#1e1e1e') && await page.$eval('#theme', s => s.value) === 'vs-dark', 'the theme survives a reload');
  for (const id of themeIds.filter(id => id !== 'auto')) {
    await page.selectOption('#theme', id);
    await page.waitForTimeout(80);
    const expected = await page.evaluate(id => window.MyDSLEditor.resolveTheme(id).editor.bg, id);
    check(await editorBg() === rgb(expected), `theme ${id} colours the editor ${expected}: ` + await editorBg());
  }
  // Auto follows the operating system.
  await page.selectOption('#theme', 'auto');
  await page.emulateMedia({ colorScheme: 'dark' });
  await page.waitForTimeout(150);
  check(await editorBg() === rgb('#1f1f1f') && await page.evaluate(() => window.mydslDebug.theme().effective) === 'dark-modern', 'auto + dark system = Dark Modern: ' + await editorBg());
  await page.emulateMedia({ colorScheme: 'light' });
  await page.waitForTimeout(150);
  check(await editorBg() === rgb('#ffffff') && await page.evaluate(() => window.mydslDebug.theme().effective) === 'light', 'auto + light system = MyDSL Light');
  // Marks and completion keep working after a theme change (the editor state was reconfigured, not replaced).
  await page.selectOption('#theme', 'monokai');
  await setText('Print(1 / 0);');
  await page.click('#run');
  await page.waitForSelector('#output .runtime-error');
  check(JSON.stringify(await marks('error')) === '["/"]', 'error marks still work in another theme');
  await focusAt((await text()).length);
  await page.keyboard.type('\nPri');
  await waitList();
  check((await listItems())[0] === 'Print', 'completion still works in another theme');
  await page.keyboard.press('Escape');
  await page.selectOption('#theme', 'auto');

  check(problems.length === 0, 'no page errors, console errors or failed requests', problems.join('\n      '));

  // ---- files: a server in a throwaway project directory ------------------------------------
  if (exe) await fileChecks(browser);
  await browser.close();
  if (failures) { console.error(`${failures} of ${passed + failures} editor checks failed`); process.exit(1); }
  console.log(`${passed} editor checks passed (real browser)`);
})().catch(e => { console.error(e); process.exit(1); });
