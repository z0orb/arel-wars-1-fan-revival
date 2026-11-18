// Writes the mangled symbol names of the DES-12 start set (universal/spec/verified.tsv) for the runner -prof option.
// A verified name matches a symbol whose demangled name equals it, or starts with it plus "(", or whose mangled name equals it.
// Usage: node tools/startset.mjs VERIFIED.tsv DEMANGLED.txt MANGLED.txt OUT.txt
import fs from 'node:fs';
const [vf, df, mf, out] = process.argv.slice(2);
const names = fs.readFileSync(vf, 'utf8').split('\n').filter(l => l && !l.startsWith('#')).map(l => l.split('\t')[0]);
const dem = fs.readFileSync(df, 'utf8').split('\n'), man = fs.readFileSync(mf, 'utf8').split('\n');
if (dem.length !== man.length) throw new Error('demangled and mangled lists differ in length');
const hits = new Set(); const missing = [];
for (const n of names) {
  let k = 0;
  for (let i = 0; i < dem.length; i++)
    if (dem[i] === n || dem[i].startsWith(n + '(') || man[i] === n) { hits.add(man[i]); k++; }
  if (!k) missing.push(n);
}
fs.writeFileSync(out, [...hits].sort().join('\n') + '\n');
console.log(`${names.length} names, ${hits.size} symbols, missing: ${missing.join(' ') || 'none'}`);
