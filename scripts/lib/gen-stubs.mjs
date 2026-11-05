// Generates patches/smali/0001-sever.patch from a pristine smali decode (docs/STUBS.md).
// Keeps every method name and signature, because native code looks them up by name.
// Usage: node scripts/lib/gen-stubs.mjs <pristine-decoded-dir> <out-dir> <patch-file>
// Every target must match exactly once, or the run fails.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const [src, out, patchFile] = process.argv.slice(2);
const id = JSON.parse(fs.readFileSync('patches/identity.json', 'utf8'));

const VOID = ['.locals 0', 'return-void'];
const INT = (v) => ['.locals 1', `const/4 v0, ${v}`, 'return v0'];
const STR = (s) => ['.locals 1', `const-string v0, "${s}"`, 'return-object v0'];
const NULL = ['.locals 1', 'const/4 v0, 0x0', 'return-object v0'];

const GL = 'com/gamevil/nexus2/NexusGLActivity';
const SK = 'com/gamevil/eruelwars/global/SkeletonLauncher';
const NA = 'com/gamevil/nexus2/Natives';
const NU = 'com/gamevil/nexus2/xml/NexusUtils';
const DR = 'com/gamevil/eruelwars/global/DRMLicensing';
const BS = 'com/gamevil/eruelwars/global/BillingService';
const PS = 'com/gamevil/nexus2/xml/ProfileSender';
const CTX = 'Landroid/content/Context;';
const S = 'Ljava/lang/String;';

// [class, method name plus signature, new body]
const STUBS = [
  // Startup gates
  [GL, 'doTorchwood()V', VOID],
  [DR, `isDomestic(${CTX})Z`, INT('0x0')],
  [DR, `getCarrierName(${CTX})${S}`, STR('')],
  [NU, `isAndroidPlayer(${CTX})Z`, INT('0x1')],
  [SK, 'sendCertificationData()V', VOID],
  [PS, `doInBackground([${S})${S}`, NULL],
  // Identity
  [NU, `getDeviceID(${CTX})${S}`, STR(id.deviceId)],
  [NU, `getAndroidID(${CTX})${S}`, STR(id.androidId)],
  [NU, `getMacAddress(${CTX})${S}`, STR(id.macAddress)],
  [NU, `getPhoneNumber(${CTX})${S}`, STR(id.phoneNumber)],
  [NU, `getOldPhoneNumber(${CTX})${S}`, STR(id.phoneNumber)],
  [NU, `getSavedPhoneNumber(${CTX})${S}`, STR(id.phoneNumber)],
  [NU, `getSimSerialNumber(${CTX})${S}`, STR(id.simSerial)],
  [NU, `checkPhonNumber(${CTX})${S}`, STR(id.deviceId)],
  [GL, 'checkDeviceID()V', VOID],
  // Network. Exactly -1: native MC_netConnect treats any other value as connected (audit Q3).
  [NA, 'netConnect()I', INT('-0x1')],
  [NA, 'isNetAvailable()I', INT('-0x1')],
  [NU, `isNetAvailable(${CTX})I`, INT('-0x1')],
  [NU, `GetFileFromHttp(${S}${S}${S}I)I`, ['.locals 1', `sget v0, L${NU};->ERROR_FAIL:I`, 'return v0']],
  [NU, `getInetHostAddress(${S})${S}`, STR('0.0.0.0')],
  [NA, `openUrl(${S})V`, VOID],
  [NA, 'showInquiryDialog()V', VOID],
  // SDKs
  [GL, `AnalyticsInitialize(${S}${CTX})V`, VOID],
  [GL, `AnalyticsTrackPageView(${S})V`, VOID],
  [NA, 'getTapjoyGPoint()V', VOID],
  [SK, 'getTapjoyGPoint()Z', INT('0x0')],
  [SK, 'initTapjoy()V', VOID],
  [SK, 'openTapjoyOffer()V', VOID],
  [NA, 'showTapjoyBtn()V', VOID],
  [GL, 'initializeC2dm()V', VOID],
  [GL, 'registerC2DM()V', VOID],
  [GL, 'unregisterC2DM()V', VOID],
  [GL, `sendRegistraionID(${S})V`, VOID],
  // Billing. Full behaviour is Phase 6. These must never report PURCHASED (audit Q6).
  [BS, 'checkBillingSupported()Z', INT('0x1')],
  [SK, 'requestPurchaceIAP()V', VOID],
  [NA, `requestIAP(I${S}[B)V`, VOID],
  [NA, `confirmBuyItem(${S}I)V`, VOID],
  [NA, `startPurchase(${S}I)V`, VOID],
  // Local alarms. Their receiver is removed from the manifest.
  [NA, 'setLocalPushNotification(I[B[B[B[BI)V', VOID],
  [NA, 'cancelLocalPushNotification(I)V', VOID],
  [NU, `setLocalPushNotification(${CTX}I${S}${S}${S}${S}I)V`, VOID],
  [NU, `cancelLocalPushNotification(${CTX}I)V`, VOID],
];

// [class, start marker, end marker (kept), description]. Removes text from start up to end.
const CUTS = [
  [SK, '    .line 306\n    new-instance v7, Lcom/gamevil/nexus2/xml/NexusXmlChecker;', '    .line 384\n    :cond_2\n', 'NexusXmlChecker, billing setup'],
  [SK, '    .line 408\n    invoke-virtual {p0}, Lcom/gamevil/eruelwars/global/SkeletonLauncher;->initializeC2dm()V', '    .line 413\n    return-void', 'C2DM and Tapjoy calls'],
  [SK, '    .line 553\n    const-string v0, "', '    .line 554\n    return-void', 'Flurry start'],
  [SK, '    .line 563\n    const/4 v0, 0x0\n\n    invoke-static {v0}, Lcom/flurry/android/FlurryAgent;->setLogEnabled(Z)V', '    .line 565\n    return-void', 'Flurry end'],
  [GL, '    .line 415\n    const-string v1, "activity"', '    .line 418\n    return-void', 'restartPackage'],
];
// The second half of the checker's locale branch, at the end of SkeletonLauncher.onCreate.
const TAIL_START = '    .line 315\n    .end local v4    # "e":Landroid/content/pm/PackageManager$NameNotFoundException;\n    :cond_3\n';

const files = new Map();
const load = (c) => {
  if (!files.has(c)) files.set(c, fs.readFileSync(path.join(src, 'smali', c + '.smali'), 'utf8'));
  return files.get(c);
};
const once = (text, needle, what) => {
  const i = text.indexOf(needle);
  if (i < 0 || text.indexOf(needle, i + 1) >= 0) throw new Error(`${what}: expected exactly one match`);
  return i;
};

for (const [c, sig, body] of STUBS) {
  const text = load(c);
  const re = new RegExp(`^\\.method [^\\n]*\\b${sig.replace(/[\\^$.*+?()[\]{}|/]/g, '\\$&')}\\n`, 'gm');
  const hits = [...text.matchAll(re)];
  if (hits.length !== 1) throw new Error(`${c} ${sig}: ${hits.length} matches`);
  const start = hits[0].index + hits[0][0].length;
  const end = text.indexOf('.end method', start);
  files.set(c, text.slice(0, start) + body.map((l) => `    ${l}\n`).join('') + text.slice(end));
}
for (const [c, a, b, what] of CUTS) {
  const text = load(c);
  const i = once(text, a, what);
  const j = text.indexOf(b, i);
  if (j < 0) throw new Error(`${what}: end marker missing`);
  files.set(c, text.slice(0, i) + text.slice(j));
}
{
  const text = load(SK);
  const i = once(text, TAIL_START, 'locale branch tail');
  const j = text.indexOf('.end method', i);
  if (!text.slice(i, j).includes('goto/16 :goto_1')) throw new Error('locale branch tail: unexpected body');
  files.set(SK, text.slice(0, i) + text.slice(j));
}

fs.rmSync(out, { recursive: true, force: true });
let patch = '';
for (const [c, text] of files) {
  const f = path.join(out, 'smali', c + '.smali');
  fs.mkdirSync(path.dirname(f), { recursive: true });
  fs.writeFileSync(f, text);
  const rel = `smali/${c}.smali`;
  const r = spawnSync('diff', ['-u', '--label', `a/${rel}`, '--label', `b/${rel}`, path.join(src, rel), f], { encoding: 'utf8', maxBuffer: 1 << 28 });
  if (r.status !== 1) throw new Error(`diff ${rel}: status ${r.status} ${r.stderr}`);
  patch += r.stdout;
}
fs.mkdirSync(path.dirname(patchFile), { recursive: true });
fs.writeFileSync(patchFile, patch);
console.log(`${patchFile}: ${STUBS.length} stubs, ${CUTS.length + 1} cuts, ${files.size} classes`);
