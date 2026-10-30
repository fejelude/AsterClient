// Independent owner-operated license service. Node 24+, built-in modules only.
import { createServer as createHttpServer } from 'node:http';
import { createServer as createHttpsServer } from 'node:https';
import { createHash, generateKeyPairSync, randomBytes, randomUUID, sign } from 'node:crypto';
import { DatabaseSync } from 'node:sqlite';
import { mkdirSync, readFileSync, writeFileSync, existsSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const hash = value => createHash('sha256').update(value).digest('hex');
const now = () => Math.floor(Date.now()/1000);
const readArgs = args => {
  const result = { command: args[0] || 'help' };
  for (let i=1; i<args.length; i++) {
    if (!args[i].startsWith('--')) throw new Error('Expected named argument');
    const key = args[i].slice(2);
    result[key] = args[i+1] && !args[i+1].startsWith('--') ? args[++i] : true;
  }
  return result;
};
function openDatabase(data) {
  mkdirSync(data, { recursive:true });
  const db = new DatabaseSync(join(data,'licenses.sqlite'));
  db.exec(`PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;
    CREATE TABLE IF NOT EXISTS licenses(id TEXT PRIMARY KEY, key_hash TEXT UNIQUE NOT NULL,
      entitlement TEXT NOT NULL, expires_at INTEGER, revoked INTEGER NOT NULL DEFAULT 0);
    CREATE TABLE IF NOT EXISTS refresh_sessions(token_hash TEXT PRIMARY KEY, license_id TEXT NOT NULL REFERENCES licenses(id),
      issued_at INTEGER NOT NULL, expires_at INTEGER NOT NULL);
    CREATE TABLE IF NOT EXISTS access_sessions(token_hash TEXT PRIMARY KEY, license_id TEXT NOT NULL REFERENCES licenses(id), expires_at INTEGER NOT NULL);
    CREATE TABLE IF NOT EXISTS nonces(nonce TEXT PRIMARY KEY, expires_at INTEGER NOT NULL);`);
  return db;
}
export function envelope(claims, privateKey) {
  const payload = Buffer.from(JSON.stringify(claims)).toString('base64url');
  return { payload, signature:sign('sha256', Buffer.from(payload), privateKey).toString('base64url') };
}
function activeLicense(license) {
  if (!license) throw Object.assign(new Error('KEY_REJECTED'),{status:401});
  if (license.revoked) throw Object.assign(new Error('LICENSE_REVOKED'),{status:403});
  if (license.expires_at !== null && license.expires_at <= now())
    throw Object.assign(new Error('LICENSE_EXPIRED'),{status:403});
  return license;
}
export function startService({ data, port=38471, development=false, cert, tlsKey, rateLimit=30 }) {
  if ((!cert || !tlsKey) && !development) throw new Error('TLS certificate and key required; loopback HTTP needs explicit development mode.');
  data = resolve(data);
  const db = openDatabase(data);
  const privateKey = readFileSync(join(data,'signing-private.pem'));
  const limits = new Map();
  function consumeRate(ip) {
    const minute=Math.floor(now()/60);
    const record=limits.get(ip);
    if (!record || record.minute!==minute) limits.set(ip,{minute,count:1});
    else if (++record.count>rateLimit) throw Object.assign(new Error('RATE_LIMITED'),{status:429});
    if (limits.size>10000) for (const [key,value] of limits) if (value.minute<minute-1) limits.delete(key);
  }
  function issue(license, nonce) {
    const issued=now(), exp=Math.min(issued+900,license.expires_at??Infinity);
    const offlineUntil=Math.min(issued+86400,license.expires_at??Infinity);
    const accessToken=randomBytes(32).toString('hex'), refreshToken=randomBytes(32).toString('hex');
    const claims={schema:1,iss:'aster-license',aud:'aster-client',licenseId:license.id,
      entitlement:license.entitlement,keyExpiresAt:license.expires_at,iat:issued,exp,offlineUntil,nonce};
    db.prepare('INSERT INTO refresh_sessions VALUES(?,?,?,?)').run(hash(refreshToken),license.id,issued,Math.min(issued+30*86400,license.expires_at??Infinity));
    db.prepare('INSERT INTO access_sessions VALUES(?,?,?)').run(hash(accessToken),license.id,exp);
    return { accessToken,refreshToken,envelope:envelope(claims,privateKey) };
  }
  const handler=async(req,res)=>{
    res.setHeader('Content-Type','application/json');res.setHeader('Cache-Control','no-store');
    res.setHeader('X-Content-Type-Options','nosniff');
    let transaction=false;
    try {
      if (req.url==='/health' && req.method==='GET') { res.end(JSON.stringify({ok:true,service:'aster-own-license',schema:1}));return; }
      consumeRate(req.socket.remoteAddress);
      if (req.url==='/api/v1/session' && req.method==='GET') {
        const bearer=req.headers.authorization?.match(/^Bearer ([a-f0-9]{64})$/)?.[1];
        if (!bearer) throw Object.assign(new Error('SESSION_REJECTED'),{status:401});
        const row=db.prepare(`SELECT l.*,a.expires_at AS access_exp FROM access_sessions a JOIN licenses l ON l.id=a.license_id WHERE a.token_hash=?`).get(hash(bearer));
        activeLicense(row);if (row.access_exp<=now()) throw Object.assign(new Error('SESSION_EXPIRED'),{status:401});
        res.end(JSON.stringify({active:true,entitlement:row.entitlement}));return;
      }
      if (/^\/updates\/(stable|preview)\.json$/.test(req.url) && req.method==='GET') {
        const channel=req.url.includes('preview')?'preview':'stable';
        const file=join(data,'releases',`${channel}.json`);
        if (!existsSync(file)) throw Object.assign(new Error('NO_RELEASE'),{status:404});
        res.end(readFileSync(file));return;
      }
      if (req.method!=='POST' || !['/api/v1/activate','/api/v1/refresh'].includes(req.url))
        throw Object.assign(new Error('NOT_FOUND'),{status:404});
      if (!String(req.headers['content-type']||'').startsWith('application/json')) throw Object.assign(new Error('JSON_REQUIRED'),{status:415});
      const chunks=[];let length=0;
      for await (const chunk of req) { length+=chunk.length;if (length>8192) throw Object.assign(new Error('REQUEST_TOO_LARGE'),{status:413});chunks.push(chunk); }
      let body;try { body=JSON.parse(Buffer.concat(chunks).toString('utf8')); }catch{throw Object.assign(new Error('INVALID_JSON'),{status:400});}
      if (!body || typeof body!=='object' || Array.isArray(body) || !/^[a-f0-9]{48}$/.test(body.nonce||''))
        throw Object.assign(new Error('INVALID_NONCE'),{status:400});
      db.exec('BEGIN IMMEDIATE');transaction=true;
      db.prepare('DELETE FROM nonces WHERE expires_at<?').run(now());
      db.prepare('DELETE FROM refresh_sessions WHERE expires_at<?').run(now());
      db.prepare('DELETE FROM access_sessions WHERE expires_at<?').run(now());
      if (db.prepare('INSERT OR IGNORE INTO nonces VALUES(?,?)').run(body.nonce,now()+900).changes!==1)
        throw Object.assign(new Error('REPLAYED_NONCE'),{status:409});
      let license;
      if (req.url==='/api/v1/activate') {
        if (typeof body.key!=='string' || !/^ASTER-[A-F0-9]{48}$/.test(body.key)) throw Object.assign(new Error('KEY_REJECTED'),{status:401});
        license=activeLicense(db.prepare('SELECT * FROM licenses WHERE key_hash=?').get(hash(body.key)));
        // Eight independent local sessions; no hardware fingerprinting or permanent device binding.
        db.prepare(`DELETE FROM refresh_sessions WHERE license_id=? AND token_hash NOT IN
          (SELECT token_hash FROM refresh_sessions WHERE license_id=? ORDER BY issued_at DESC LIMIT 7)`).run(license.id,license.id);
      } else {
        if (typeof body.refreshToken!=='string' || !/^[a-f0-9]{64}$/.test(body.refreshToken)) throw Object.assign(new Error('SESSION_REJECTED'),{status:401});
        const session=db.prepare('SELECT * FROM refresh_sessions WHERE token_hash=?').get(hash(body.refreshToken));
        if (!session) throw Object.assign(new Error('SESSION_REJECTED'),{status:401});
        license=activeLicense(db.prepare('SELECT * FROM licenses WHERE id=?').get(session.license_id));
        db.prepare('DELETE FROM refresh_sessions WHERE token_hash=?').run(hash(body.refreshToken));
      }
      const result=issue(license,body.nonce);db.exec('COMMIT');transaction=false;
      res.end(JSON.stringify(result));
    } catch(error) {
      if (transaction) db.exec('ROLLBACK');
      // Do not echo request data, database errors, purchase keys or tokens.
      const status=Number(error.status)||500;res.statusCode=status;
      res.end(JSON.stringify({error:status===500?'SERVICE_UNAVAILABLE':error.message}));
    }
  };
  const server=cert&&tlsKey?createHttpsServer({cert:readFileSync(cert),key:readFileSync(tlsKey),minVersion:'TLSv1.2'},handler):createHttpServer(handler);
  server.requestTimeout=10000;server.headersTimeout=10000;
  server.listen(port,'127.0.0.1');
  server.on('close',()=>db.close());
  return server;
}
function cli() {
  const args=readArgs(process.argv.slice(2));const data=resolve(String(args.data||join(import.meta.dirname,'data')));
  if (args.command==='init') {
    mkdirSync(data,{recursive:true});
    if (existsSync(join(data,'signing-private.pem'))) throw new Error('Existing signing keys preserved; initialization refused.');
    const keys=generateKeyPairSync('rsa',{modulusLength:3072,publicKeyEncoding:{type:'spki',format:'pem'},privateKeyEncoding:{type:'pkcs8',format:'pem'}});
    writeFileSync(join(data,'signing-private.pem'),keys.privateKey,{mode:0o600,flag:'wx'});
    writeFileSync(join(data,'signing-public.pem'),keys.publicKey,{flag:'wx'});
    openDatabase(data).close();console.log('Own-service signing keys and database created. Keep the private key on the server.');
  } else if (args.command==='issue') {
    if (!args['key-file']) throw new Error('--key-file is required so purchase keys are not printed into logs.');
    const db=openDatabase(data);const id=randomUUID(),key='ASTER-'+randomBytes(24).toString('hex').toUpperCase();
    const entitlement=args.entitlement||'lifetime',expiry=args.expires?Number(args.expires):null;
    if (!['lifetime','subscription'].includes(entitlement) || (expiry!==null&&(!Number.isSafeInteger(expiry)||expiry<=now())) || (entitlement==='subscription'&&expiry===null))
      throw new Error('Invalid entitlement or expiry.');
    // Write the key before committing an entitlement; never overwrite an existing key file.
    writeFileSync(resolve(String(args['key-file'])),key+'\n',{mode:0o600,flag:'wx'});
    db.prepare('INSERT INTO licenses(id,key_hash,entitlement,expires_at) VALUES(?,?,?,?)').run(id,hash(key),entitlement,expiry);db.close();
    console.log(JSON.stringify({licenseId:id,entitlement,expiresAt:expiry,keyWritten:true}));
  } else if (args.command==='revoke') {
    if (!args.id) throw new Error('--id is required.');const db=openDatabase(data);
    const changed=db.prepare('UPDATE licenses SET revoked=1 WHERE id=?').run(String(args.id)).changes;db.close();
    console.log(JSON.stringify({revoked:changed===1}));
  } else if (args.command==='serve') {
    const server=startService({data,port:Number(args.port||38471),development:args['allow-loopback-http']===true,
      cert:args['tls-cert'],tlsKey:args['tls-key']});
    server.once('listening',()=>console.log(JSON.stringify({ready:true,port:server.address().port,loopbackOnly:true,tls:!!args['tls-cert']})));
    for(const signal of ['SIGINT','SIGTERM'])process.on(signal,()=>server.close());
  } else if (args.command==='sign-release') {
    if (!args.manifest || !args.output) throw new Error('--manifest and --output required.');
    const claims=JSON.parse(readFileSync(resolve(String(args.manifest)),'utf8'));
    if (claims.kind!=='aster-release'||claims.schema!==1)throw new Error('Invalid release manifest identity.');
    writeFileSync(resolve(String(args.output)),JSON.stringify(envelope(claims,readFileSync(join(data,'signing-private.pem'))),null,2)+'\n',{flag:'wx'});
    console.log('Release envelope signed.');
  } else console.log('Commands: init, issue --key-file FILE, revoke --id ID, serve --tls-cert FILE --tls-key FILE, sign-release --manifest FILE --output FILE. All accept --data DIR. Development serve: --allow-loopback-http.');
}
if (process.argv[1] && resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
  try { cli(); } catch(error) { console.error(error.message);process.exitCode=1; }
}
