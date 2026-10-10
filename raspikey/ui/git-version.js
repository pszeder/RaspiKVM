const chidProcess = require('child_process');
const { writeFileSync } = require('fs');
var fs = require('fs');
const sourceDirectory = process.env.RASPIKEY_SOURCE_DIR || process.cwd();
const git = command => chidProcess.execSync(command, {cwd: sourceDirectory});

const longSHA = git("git rev-parse HEAD").toString().trim();
const shortSHA = git("git rev-parse --short HEAD").toString().trim();
const branch = git('git rev-parse --abbrev-ref HEAD').toString().trim();
const authorName = git("git log -1 --pretty=format:'%an'").toString().trim();
const commitTime = git("git log -1 --pretty=format:'%cd'").toString().trim();
const commitMsg = git("git log -1 --pretty=%B").toString().trim();
const totalCommitCount = git("git rev-list --count HEAD").toString().trim();
const majorMinor = fs.readFileSync('../version.txt','utf8').trim();

const versionInfo = {
    shortSHA: shortSHA,
    SHA : longSHA,
    branch: branch,
    lastCommitAuthor: authorName,
    lastCommitTime: commitTime,
    lastCommitMessage: commitMsg,
    lastCommitNumber: totalCommitCount,
    majorMinor: majorMinor
}

const versionInfoJson = JSON.stringify(versionInfo, null, 2);

writeFileSync('src/.git-version.json', versionInfoJson);