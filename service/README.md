# Optional owner-operated service

The client works locally without a purchased key. This independent service is for an owner who chooses to require entitlements or host signed release metadata. It uses Node 24+ built-in HTTP(S), cryptography and SQLite modules; no npm install is needed. It has no connection to Nitro's services.

Run commands from the project or portable-package directory. Use a private data directory outside any public web root:

```powershell
node service/server.mjs init --data C:/AsterService/private
node service/server.mjs issue --data C:/AsterService/private --key-file C:/AsterService/new-key.txt
node service/server.mjs serve --data C:/AsterService/private --allow-loopback-http --port 38471
```

The last command is a local development fixture. Configure the client with `http://127.0.0.1:38471` and `signing-public.pem`, then apply the service settings. Purchase keys are written once to the named new file instead of being printed into logs. `issue` prints the license ID for later revocation. A subscription also needs `--entitlement subscription --expires <Unix-seconds>`; lifetime is the default.

For deployment, provide `--tls-cert <certificate.pem> --tls-key <private-key.pem>` in place of `--allow-loopback-http`. The service always listens on 127.0.0.1; expose it only through an owner-configured HTTPS reverse proxy or equivalent controlled host. Client endpoints require HTTPS except explicit loopback HTTP fixtures. No public host, certificate, checkout or firewall changes are provisioned automatically.

Keep `signing-private.pem`, TLS private keys, the SQLite database/WAL and issued purchase keys on the server. Only the public signing PEM belongs with client configuration. Windows filesystem permissions and backups are the operator's responsibility. Initialization refuses to overwrite existing signing keys. Do not publish the service data directory.

Access entitlements last at most 15 minutes. Refresh rotates opaque tokens, rejects reuse and can last at most 30 days. Offline grace is at most 24 hours and never exceeds the purchased entitlement's expiry. Revocation rejects subsequent online activation, access and refresh requests; an already issued signed offline entitlement can remain usable until its bounded expiry. There is no hardware fingerprinting.

```powershell
node service/server.mjs revoke --data C:/AsterService/private --id <license-id>
node service/server.mjs sign-release --data C:/AsterService/private --manifest release-claims.json --output stable.json
```

Release claims must use `kind: "aster-release"`, `schema: 1` and the client-validated fields demonstrated in `service/tests.mjs` in the source repository. Publish the signed envelope under the service data directory's `releases/stable.json` or `releases/preview.json`. Host the referenced signed executable separately. Feed signing does not replace the client's SHA-256 and trusted Authenticode publisher checks. The development executable is unsigned and cannot satisfy a production trusted-publisher policy.

From a source checkout with `AsterTests` built, run `node --test service/tests.mjs`. The suite uses disposable data and performs real native client/vault/update/offline integration. The portable package contains the service runtime and this guide; developer test binaries and private data are not packaged.
