// Finds a UI element in a uiautomator dump by its text (case-insensitive regex) and prints the
// centre of its bounds: "X Y". Exit 1 if no element matches.
// Usage: node tools/ui-find.mjs DUMP.xml REGEX
import fs from 'node:fs';
const [file, re] = process.argv.slice(2);
const xml = fs.readFileSync(file, 'utf8');
const rx = new RegExp(re, 'i');
for (const m of xml.matchAll(/<node [^>]*>/g)) {
  const t = /\btext="([^"]*)"/.exec(m[0]);
  const b = /\bbounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"/.exec(m[0]);
  if (!t || !b || !rx.test(t[1])) continue;
  console.log(`${(+b[1] + +b[3]) >> 1} ${(+b[2] + +b[4]) >> 1}`);
  process.exit(0);
}
process.exit(1);
