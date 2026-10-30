import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { spawn } from 'node:child_process';
import { createHash, generateKeyPairSync, randomBytes, verify } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import { startService, envelope } from './server.mjs';

const sha = x=>createHash('sha256').update(x).digest('hex');
const nonce = ()=>randomBytes(24).toString('hex');
const time = ()=>Math.floor(Date.now()/1000);
function child(executable,args) {
  return new Promise((accept,reject)=>{
    const p=spawn(executable,args,{cwd:resolve(import.meta.dirname,'..'),windowsHide:true});let output='';
    p.stdout.on('data',chunk=>output+=chunk);p.stderr.on('data',chunk=>output+=chunk);p.once('error',reject);
    p.once('exit',code=>code===0?accept(output):reject(new Error(`Native integration test failed (${code}): ${output}`)));
  });
}
test('Independent owner service and native Windows client integration', {timeout:90000}, async t=>{
  const data=resolve(import.meta.dirname,'..','test-results','private',`service-${Date.now()}`);mkdirSync(data,{recursive:true});
  const keys=generateKeyPairSync('rsa',{modulusLength:3072,publicKeyEncoding:{type:'spki',format:'pem'},privateKeyEncoding:{type:'pkcs8',format:'pem'}});
  writeFileSync(join(data,'signing-private.pem'),keys.privateKey,{mode:0o600});writeFileSync(join(data,'signing-public.pem'),keys.publicKey);
  const server=startService({data,port:0,development:true,rateLimit:200});await once(server,'listening');
  const endpoint=`http://127.0.0.1:${server.address().port}`;const db=new DatabaseSync(join(data,'licenses.sqlite'));
  const raw='ASTER-'+randomBytes(24).toString('hex').toUpperCase(),id='integration-lifetime';
  db.prepare('INSERT INTO licenses VALUES(?,?,?,?,?)').run(id,sha(raw),'lifetime',null,0);
  const keyFile=join(data,'purchase-key.txt');writeFileSync(keyFile,raw,{mode:0o600});
  const post=async(route,body)=>{
    const response=await fetch(endpoint+route,{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify(body)});
    return {status:response.status,body:await response.json()};
  };
  let first,rotated;
  try {
    await t.test('TLS is mandatory outside explicit loopback development mode',()=>{
      assert.throws(()=>startService({data,port:0}),/TLS/);
    });
    await t.test('Activation issues an independently signed, nonce-bound entitlement',async()=>{
      const requestNonce=nonce();first=await post('/api/v1/activate',{key:raw,nonce:requestNonce});assert.equal(first.status,200);
      const e=first.body.envelope;assert.ok(verify('sha256',Buffer.from(e.payload),keys.publicKey,Buffer.from(e.signature,'base64url')));
      const claims=JSON.parse(Buffer.from(e.payload,'base64url'));assert.equal(claims.nonce,requestNonce);assert.equal(claims.entitlement,'lifetime');
      assert.equal(claims.offlineUntil-claims.iat,86400);assert.ok(claims.exp>time());
      const altered=e.payload.slice(0,-1)+(e.payload.endsWith('A')?'B':'A');assert.ok(!verify('sha256',Buffer.from(altered),keys.publicKey,Buffer.from(e.signature,'base64url')));
    });
    await t.test('Access session checks a real opaque token',async()=>{
      const response=await fetch(endpoint+'/api/v1/session',{headers:{authorization:`Bearer ${first.body.accessToken}`}});assert.equal(response.status,200);
    });
    await t.test('Refresh rotates tokens and rejects reuse',async()=>{
      rotated=await post('/api/v1/refresh',{refreshToken:first.body.refreshToken,nonce:nonce()});assert.equal(rotated.status,200);
      assert.ok(rotated.body.refreshToken!==first.body.refreshToken);
      const reused=await post('/api/v1/refresh',{refreshToken:first.body.refreshToken,nonce:nonce()});assert.equal(reused.status,401);
    });
    await t.test('A successful request nonce cannot be replayed',async()=>{
      const n=nonce();assert.equal((await post('/api/v1/activate',{key:raw,nonce:n})).status,200);
      assert.equal((await post('/api/v1/activate',{key:raw,nonce:n})).status,409);
    });
    await t.test('Invalid keys, malformed JSON and missing nonce return explicit failures',async()=>{
      assert.equal((await post('/api/v1/activate',{key:'bad',nonce:nonce()})).status,401);
      assert.equal((await post('/api/v1/activate',{key:raw})).status,400);
      const response=await fetch(endpoint+'/api/v1/activate',{method:'POST',headers:{'content-type':'application/json'},body:'{invalid'});assert.equal(response.status,400);
    });
    await t.test('Revocation invalidates live access and refresh sessions',async()=>{
      db.prepare('UPDATE licenses SET revoked=1 WHERE id=?').run(id);
      assert.equal((await post('/api/v1/refresh',{refreshToken:rotated.body.refreshToken,nonce:nonce()})).body.error,'LICENSE_REVOKED');
      assert.equal((await fetch(endpoint+'/api/v1/session',{headers:{authorization:`Bearer ${first.body.accessToken}`}})).status,403);
      db.prepare('UPDATE licenses SET revoked=0 WHERE id=?').run(id);
    });
    await t.test('Expired purchase keys never grant offline grace',async()=>{
      const expired='ASTER-'+randomBytes(24).toString('hex').toUpperCase();db.prepare('INSERT INTO licenses VALUES(?,?,?,?,?)').run('expired',sha(expired),'subscription',time()-1,0);
      assert.equal((await post('/api/v1/activate',{key:expired,nonce:nonce()})).body.error,'LICENSE_EXPIRED');
    });
    await t.test('Purchase and refresh tokens are hashed in the database',()=>{
      assert.ok(!db.prepare('SELECT key_hash FROM licenses WHERE id=?').get(id).key_hash.includes(raw));
      assert.equal(db.prepare('SELECT COUNT(*) AS count FROM refresh_sessions WHERE token_hash=?').get(rotated.body.refreshToken).count,0);
    });
    const native=resolve(import.meta.dirname,'..','build','Release','AsterTests.exe');
    await t.test('Native activation, real Windows vault and remembered-session flow',async()=>{
      await child(native,['--license-prepare',endpoint,join(data,'signing-public.pem'),keyFile]);
    });
    await t.test('Native update checker verifies a real service-signed feed',async()=>{
      const release={schema:1,kind:'aster-release',platform:'windows-x64',channel:'stable',version:'0.2.0',
        issuedAt:time(),expiresAt:time()+3600,url:'https://example.com/AsterClient-0.2.0.exe',sha256:'0'.repeat(64),notes:'Integration fixture metadata only.'};
      mkdirSync(join(data,'releases'));writeFileSync(join(data,'releases','stable.json'),JSON.stringify(envelope(release,keys.privateKey)));
      await child(native,['--update-integration',endpoint+'/updates/stable.json',join(data,'signing-public.pem')]);
      const damaged=envelope(release,keys.privateKey);damaged.payload=damaged.payload.slice(1);writeFileSync(join(data,'releases','stable.json'),JSON.stringify(damaged));
      await child(native,['--update-reject',endpoint+'/updates/stable.json',join(data,'signing-public.pem')]);
    });
    await t.test('Native client uses signed offline grace after the server stops',async()=>{
      db.close();await new Promise(resolve=>server.close(resolve));
      await child(native,['--license-offline',endpoint,join(data,'signing-public.pem'),keyFile]);
    });
  } finally {
    if(server.listening){db.close();await new Promise(resolve=>server.close(resolve));}
  }
});
