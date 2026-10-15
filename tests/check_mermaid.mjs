// Parse every ```mermaid block in the repository's markdown.
//
// GitHub renders these itself, and a syntax error there costs the whole
// README its rich display - the page falls back to "Unable to render rich
// display" rather than pointing at the offending line. This runs the real
// mermaid parser over each block so CI says which file and line instead.
//
//   npm install mermaid jsdom
//   node tests/check_mermaid.mjs
//
// Note: mermaid treats ';' as a statement separator, so a semicolon inside
// note or label text breaks the diagram. That is what this check first
// caught.

import { JSDOM } from 'jsdom';
import fs from 'fs';
import path from 'path';

const dom = new JSDOM('<!doctype html><html><body></body></html>');
global.window = dom.window;
global.document = dom.window.document;
Object.defineProperty(global, 'navigator', {
  value: dom.window.navigator,
  configurable: true,
});

const mermaid = (await import('mermaid')).default;
mermaid.initialize({ startOnLoad: false, securityLevel: 'loose' });

function markdownFiles(dir) {
  const found = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    if (entry.name === '.git' || entry.name === 'node_modules') continue;
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) found.push(...markdownFiles(full));
    else if (entry.name.endsWith('.md')) found.push(full);
  }
  return found;
}

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
let blocks = 0;
let failures = 0;

for (const file of markdownFiles(root)) {
  const lines = fs.readFileSync(file, 'utf8').split('\n');
  let start = -1;
  for (let i = 0; i < lines.length; i++) {
    if (start < 0 && /^\s*```mermaid\s*$/.test(lines[i])) {
      start = i;
    } else if (start >= 0 && /^\s*```\s*$/.test(lines[i])) {
      const body = lines.slice(start + 1, i).join('\n');
      const rel = path.relative(root, file);
      blocks++;
      try {
        await mermaid.parse(body);
        console.log(`ok    ${rel}:${start + 1}`);
      } catch (err) {
        failures++;
        const where = err?.hash?.loc?.first_line;
        console.log(`FAIL  ${rel}:${start + 1}` +
                    (where ? ` (block line ${where}, file line ${start + 1 + where})` : ''));
        console.log(String(err?.message || err).split('\n').map(l => '      ' + l).join('\n'));
      }
      start = -1;
    }
  }
}

console.log(`\n${blocks} mermaid block(s), ${failures} failure(s)`);
process.exit(failures ? 1 : 0);
