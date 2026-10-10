// Build on Windows or Linux with Node 22. Upload only source and the built UI to the Pi.
const fs = require('fs'), path = require('path'), crypto = require('crypto'), cp = require('child_process');
const repo = path.resolve(__dirname, '../..');
const out = path.resolve(process.argv[2] || path.join(repo, 'dist'));
if (Number(process.versions.node.split('.')[0]) !== 22) throw Error('Use Node.js 22 for this pinned legacy frontend build.');
const npm = path.join(path.dirname(process.execPath), 'node_modules/npm/bin/npm-cli.js');
const npmExe = process.platform === 'win32' ? process.execPath : 'npm';
const npmPrefix = process.platform === 'win32' ? [npm] : [];
const ui = path.join(repo, 'raspikey/ui');
const sourceRepo = process.env.RASPIKEY_SOURCE_DIR || repo;
function run(exe, args, cwd = repo, env = process.env) {
    const r = cp.spawnSync(exe, args, {cwd, env, stdio:'inherit'});
    if (r.error) throw r.error;
    if (r.status !== 0) throw Error(exe + ' failed: ' + r.status);
}
// WSL must interpret the Windows checkout's CRLF files like Windows Git does.
// Otherwise an unchanged checkout is incorrectly labelled as a dirty release.
function git(args) { return cp.execFileSync('git', ['-c','core.autocrlf=true',...args], {cwd:sourceRepo, encoding:'utf8'}).trim(); }
run(npmExe, [...npmPrefix, 'ci', '--ignore-scripts', '--no-audit', '--no-fund'], ui);
run(npmExe, [...npmPrefix, 'run', 'build'], ui, {...process.env, NODE_OPTIONS:'--openssl-legacy-provider'});
const stage = fs.mkdtempSync(path.join(require('os').tmpdir(), 'raspikey-bundle-'));
function copy(from, to, skip = new Set()) {
    fs.mkdirSync(to, {recursive:true});
    for (const e of fs.readdirSync(from, {withFileTypes:true})) {
        if (skip.has(e.name)) continue;
        if (e.isSymbolicLink()) throw Error('Unexpected source symlink: '+e.name);
        const a=path.join(from,e.name), b=path.join(to,e.name);
        if (e.isDirectory()) copy(a,b,skip); else fs.copyFileSync(a,b);
    }
}
try {
    copy(path.join(repo,'raspikey'), path.join(stage,'raspikey'), new Set(['ui','build']));
    copy(__dirname, path.join(stage,'scripts/development'), new Set(['__pycache__']));
    copy(path.join(ui,'dist'), path.join(stage,'html'));
    const files={};
    function hash(dir) {
        for (const e of fs.readdirSync(dir,{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name))) {
            const f=path.join(dir,e.name);
            if (e.isDirectory()) hash(f);
            else files[path.relative(stage,f).split(path.sep).join('/')]=crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
        }
    }
    hash(stage);
    const fingerprint=crypto.createHash('sha256').update(JSON.stringify(files)).digest('hex').slice(0,12);
    const stamp=new Date().toISOString().replace(/[-:]/g,'').replace(/\.\d+Z$/,'Z');
    const release=stamp+'-'+git(['rev-parse','--short','HEAD'])+'-'+fingerprint;
    const m={schema:1,release,sourceCommit:git(['rev-parse','HEAD']),dirty:git(['status','--porcelain']).length>0,
        node:process.version,files};
    fs.writeFileSync(path.join(stage,'manifest.json'),JSON.stringify(m,null,2)+'\n');
    fs.mkdirSync(out,{recursive:true});
    const archive=path.join(out,'raspikey-dev-'+release+'.tar.gz');
    run('tar',['-czf',archive,'-C',stage,'.']);
    const sha=crypto.createHash('sha256').update(fs.readFileSync(archive)).digest('hex');
    fs.writeFileSync(archive+'.sha256',sha+'  '+path.basename(archive)+'\n');
    console.log('Development bundle: '+archive);
    console.log('SHA256: '+sha);
} finally { fs.rmSync(stage,{recursive:true,force:true}); }
