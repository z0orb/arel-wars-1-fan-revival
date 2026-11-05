// Binary AndroidManifest.xml patcher. apktool cannot rebuild this manifest (see HANDOFF.md),
// so manifest changes are made directly on the compiled file.
//
// Usage: node axml.mjs in.xml out.xml ops.json [ops.json ...]
// Ops, applied in order. Every op must match at least one element, or the run fails:
//   {"op":"remove-element","tag":"uses-permission","name":"android.permission.INTERNET"}
//   {"op":"remove-element","tag":"intent-filter","hasChild":{"tag":"action","name":"..."}}
//   {"op":"set-attr","tag":"uses-sdk","attr":"targetSdkVersion","id":"0x01010270","type":"int","value":28}
// "name" optionally narrows a match to elements whose android:name equals it.
// Types: int, hex (flags), bool, string.
import fs from 'node:fs';

const ANDROID_NS = 'http://schemas.android.com/apk/res/android';
const NAME_ID = 0x01010003;
const NONE = 0xffffffff;
const TYPE = { string: 0x03, int: 0x10, hex: 0x11, bool: 0x12 };

export function parse(buf) {
  if (buf.readUInt16LE(0) !== 0x0003) throw new Error('not a binary XML file');
  const doc = { strings: [], resMap: [], nodes: [] };
  let p = buf.readUInt16LE(2);
  const end = buf.readUInt32LE(4);
  while (p < end) {
    const type = buf.readUInt16LE(p);
    const size = buf.readUInt32LE(p + 4);
    const chunk = buf.subarray(p, p + size);
    if (type === 0x0001) readPool(chunk, doc);
    else if (type === 0x0180) for (let i = 8; i < size; i += 4) doc.resMap.push(chunk.readUInt32LE(i));
    else if (type >= 0x0100 && type <= 0x0104) doc.nodes.push(readNode(type, chunk));
    else throw new Error(`unknown chunk 0x${type.toString(16)}`);
    p += size;
  }
  return doc;
}

function readPool(c, doc) {
  const count = c.readUInt32LE(8);
  if (c.readUInt32LE(12) !== 0) throw new Error('styled string pool is not supported');
  if (c.readUInt32LE(16) & 0x100) throw new Error('UTF-8 string pool is not supported');
  const start = c.readUInt32LE(20);
  for (let i = 0; i < count; i++) {
    let q = start + c.readUInt32LE(28 + i * 4);
    let len = c.readUInt16LE(q);
    q += 2;
    if (len & 0x8000) {
      len = ((len & 0x7fff) << 16) | c.readUInt16LE(q);
      q += 2;
    }
    doc.strings.push(c.toString('utf16le', q, q + len * 2));
  }
}

function readNode(type, c) {
  const n = { type, line: c.readUInt32LE(8), comment: c.readUInt32LE(12) };
  if (type === 0x0102) {
    n.ns = c.readUInt32LE(16);
    n.name = c.readUInt32LE(20);
    const attrStart = c.readUInt16LE(24);
    const attrSize = c.readUInt16LE(26);
    if (attrStart !== 20 || attrSize !== 20) throw new Error('unexpected attribute layout');
    const count = c.readUInt16LE(28);
    n.idIndex = c.readUInt16LE(30);
    n.classIndex = c.readUInt16LE(32);
    n.styleIndex = c.readUInt16LE(34);
    n.attrs = [];
    for (let i = 0; i < count; i++) {
      const q = 36 + i * 20;
      n.attrs.push({
        ns: c.readUInt32LE(q),
        name: c.readUInt32LE(q + 4),
        raw: c.readUInt32LE(q + 8),
        valueSize: c.readUInt16LE(q + 12),
        res0: c.readUInt8(q + 14),
        vtype: c.readUInt8(q + 15),
        data: c.readUInt32LE(q + 16),
      });
    }
  } else if (type === 0x0104) {
    n.body = Buffer.from(c.subarray(16));
  } else {
    n.a = c.readUInt32LE(16);
    n.b = c.readUInt32LE(20);
  }
  return n;
}

export function serialize(doc) {
  const parts = [writePool(doc.strings)];
  const map = Buffer.alloc(8 + doc.resMap.length * 4);
  map.writeUInt16LE(0x0180, 0);
  map.writeUInt16LE(8, 2);
  map.writeUInt32LE(map.length, 4);
  doc.resMap.forEach((v, i) => map.writeUInt32LE(v, 8 + i * 4));
  parts.push(map);
  for (const n of doc.nodes) parts.push(writeNode(n));
  const total = 8 + parts.reduce((s, b) => s + b.length, 0);
  const head = Buffer.alloc(8);
  head.writeUInt16LE(0x0003, 0);
  head.writeUInt16LE(8, 2);
  head.writeUInt32LE(total, 4);
  return Buffer.concat([head, ...parts]);
}

function writePool(strings) {
  const bodies = strings.map((s) => {
    const text = Buffer.from(s, 'utf16le');
    const len = s.length;
    const prefix = len >= 0x8000 ? 4 : 2;
    const b = Buffer.alloc(prefix + text.length + 2);
    if (prefix === 4) {
      b.writeUInt16LE(((len >>> 16) & 0x7fff) | 0x8000, 0);
      b.writeUInt16LE(len & 0xffff, 2);
    } else b.writeUInt16LE(len, 0);
    text.copy(b, prefix);
    return b;
  });
  const start = 28 + strings.length * 4;
  const raw = bodies.reduce((s, b) => s + b.length, 0);
  const pad = (4 - (raw % 4)) % 4;
  const c = Buffer.alloc(start + raw + pad);
  c.writeUInt16LE(0x0001, 0);
  c.writeUInt16LE(28, 2);
  c.writeUInt32LE(c.length, 4);
  c.writeUInt32LE(strings.length, 8);
  c.writeUInt32LE(start, 20);
  let off = 0;
  bodies.forEach((b, i) => {
    c.writeUInt32LE(off, 28 + i * 4);
    b.copy(c, start + off);
    off += b.length;
  });
  return c;
}

function writeNode(n) {
  let c;
  if (n.type === 0x0102) {
    c = Buffer.alloc(36 + n.attrs.length * 20);
    c.writeUInt32LE(n.ns, 16);
    c.writeUInt32LE(n.name, 20);
    c.writeUInt16LE(20, 24);
    c.writeUInt16LE(20, 26);
    c.writeUInt16LE(n.attrs.length, 28);
    c.writeUInt16LE(n.idIndex, 30);
    c.writeUInt16LE(n.classIndex, 32);
    c.writeUInt16LE(n.styleIndex, 34);
    n.attrs.forEach((a, i) => {
      const q = 36 + i * 20;
      c.writeUInt32LE(a.ns, q);
      c.writeUInt32LE(a.name, q + 4);
      c.writeUInt32LE(a.raw, q + 8);
      c.writeUInt16LE(a.valueSize, q + 12);
      c.writeUInt8(a.res0, q + 14);
      c.writeUInt8(a.vtype, q + 15);
      c.writeUInt32LE(a.data, q + 16);
    });
  } else if (n.type === 0x0104) {
    c = Buffer.alloc(16 + n.body.length);
    n.body.copy(c, 16);
  } else {
    c = Buffer.alloc(24);
    c.writeUInt32LE(n.a, 16);
    c.writeUInt32LE(n.b, 20);
  }
  c.writeUInt16LE(n.type, 0);
  c.writeUInt16LE(16, 2);
  c.writeUInt32LE(c.length, 4);
  c.writeUInt32LE(n.line, 8);
  c.writeUInt32LE(n.comment, 12);
  return c;
}

// Finds a string index, adding the string if needed.
function stringIndex(doc, s) {
  const i = doc.strings.indexOf(s);
  if (i >= 0) return i;
  doc.strings.push(s);
  return doc.strings.length - 1;
}

// Returns the string index of an attribute name tied to a resource id. These names must sit in
// the string range covered by the resource map, so a new one is inserted at its end and every
// later string reference moves up by one.
function attrNameIndex(doc, name, id) {
  const found = doc.resMap.indexOf(id);
  if (found >= 0) return found;
  const at = doc.resMap.length;
  doc.strings.splice(at, 0, name);
  doc.resMap.push(id);
  const up = (v) => (v !== NONE && v >= at ? v + 1 : v);
  for (const n of doc.nodes) {
    n.comment = up(n.comment);
    if (n.type === 0x0102) {
      n.ns = up(n.ns);
      n.name = up(n.name);
      for (const a of n.attrs) {
        a.ns = up(a.ns);
        a.name = up(a.name);
        a.raw = up(a.raw);
        if (a.vtype === TYPE.string) a.data = up(a.data);
      }
    } else if (n.type === 0x0103 || n.type === 0x0100 || n.type === 0x0101) {
      n.a = up(n.a);
      n.b = up(n.b);
    } else if (n.type === 0x0104) {
      n.body.writeUInt32LE(up(n.body.readUInt32LE(0)), 0);
    }
  }
  return at;
}

function matches(doc, n, op) {
  if (n.type !== 0x0102 || doc.strings[n.name] !== op.tag) return false;
  if (op.name === undefined) return true;
  const a = n.attrs.find((x) => doc.resMap[x.name] === NAME_ID);
  return !!a && a.raw !== NONE && doc.strings[a.raw] === op.name;
}

export function apply(doc, op) {
  let hits = 0;
  if (op.op === 'remove-element') {
    for (let i = 0; i < doc.nodes.length; i++) {
      if (!matches(doc, doc.nodes[i], op)) continue;
      let depth = 0;
      let j = i;
      for (; j < doc.nodes.length; j++) {
        if (doc.nodes[j].type === 0x0102) depth++;
        if (doc.nodes[j].type === 0x0103 && --depth === 0) break;
      }
      if (j === doc.nodes.length) throw new Error('unbalanced element');
      if (op.hasChild && !doc.nodes.slice(i, j + 1).some((c) => matches(doc, c, op.hasChild))) continue;
      doc.nodes.splice(i, j - i + 1);
      i--;
      hits++;
    }
  } else if (op.op === 'set-attr') {
    if (!(op.type in TYPE)) throw new Error(`unknown type ${op.type}`);
    const id = Number(op.id);
    for (const n of doc.nodes) {
      if (!matches(doc, n, op)) continue;
      const nameIdx = attrNameIndex(doc, op.attr, id);
      const nsIdx = stringIndex(doc, ANDROID_NS);
      let a = n.attrs.find((x) => doc.resMap[x.name] === id);
      if (!a) {
        a = { ns: nsIdx, name: nameIdx, raw: NONE, valueSize: 8, res0: 0, vtype: 0, data: 0 };
        // The platform expects attributes sorted by resource id.
        const pos = n.attrs.findIndex((x) => x.name >= doc.resMap.length || doc.resMap[x.name] > id);
        const at = pos < 0 ? n.attrs.length : pos;
        n.attrs.splice(at, 0, a);
        for (const k of ['idIndex', 'classIndex', 'styleIndex']) if (n[k] > at) n[k]++;
      }
      a.vtype = TYPE[op.type];
      if (op.type === 'string') {
        a.raw = stringIndex(doc, op.value);
        a.data = a.raw;
      } else {
        a.raw = NONE;
        a.data = op.type === 'bool' ? (op.value ? NONE : 0) : Number(op.value) >>> 0;
      }
      hits++;
    }
  } else throw new Error(`unknown op ${op.op}`);
  if (!hits) throw new Error(`op matched nothing: ${JSON.stringify(op)}`);
  return hits;
}

if (process.argv[1] && process.argv[1].endsWith('axml.mjs')) {
  const [input, output, ...opFiles] = process.argv.slice(2);
  if (!output) throw new Error('usage: axml.mjs in out [ops.json ...]');
  const original = fs.readFileSync(input);
  const ops = opFiles.flatMap((f) => JSON.parse(fs.readFileSync(f, 'utf8')));
  if (!ops.length) {
    fs.writeFileSync(output, original); // no ops: keep the exact original bytes
  } else {
    const doc = parse(original);
    for (const op of ops) console.log(`axml: ${apply(doc, op)} x ${JSON.stringify(op)}`);
    fs.writeFileSync(output, serialize(doc));
  }
}
