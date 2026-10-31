/* Focused browser integration test, not desktop UI automation. */
const fs = require('node:fs');
const path = require('node:path');
const {spawn} = require('node:child_process');
const assert = require('node:assert/strict');
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const root = __dirname;
const output = path.resolve(process.env.WEBCAM_TEST_OUTPUT || path.join(root, '../../build/webcam-feasibility'));
fs.mkdirSync(output, {recursive:true});
const origin = process.env.WEBCAM_TEST_ORIGIN || 'https://lagdaemon.com';
const bind = process.env.WEBCAM_TEST_BIND;
if(!bind)throw Error('WEBCAM_TEST_BIND must specify the PC LAN IPv4 address');
const endpointLog = fs.openSync(path.join(output, 'endpoint.log'),'w');
const invitationFile=path.join(output,'invitation.private.json');
if(fs.existsSync(invitationFile))fs.unlinkSync(invitationFile);
const endpoint=spawn(process.env.PYTHON_EXECUTABLE || 'python', [path.join(root,'endpoint.py'),
  '--bind',bind,'--origin',origin,'--invitation',invitationFile,'--lifetime','180'],
  {stdio:['ignore',endpointLog,endpointLog],windowsHide:true});
const result={origin, syntheticMedia:true, physicalAndroid:false, checks:[], errors:[]};
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
let browser;
(async()=>{
  for(let i=0;i<100&&!fs.existsSync(invitationFile);++i){if(endpoint.exitCode!==null)throw Error('Endpoint exited');await delay(100);}
  assert(fs.existsSync(invitationFile),'Endpoint invitation ready');
  const invitation=JSON.parse(fs.readFileSync(invitationFile,'utf8'));
  browser=await chromium.launch({executablePath:process.env.BROWSER_EXECUTABLE,headless:true,chromiumSandbox:true});
  result.browserVersion=browser.version();
  const context=await browser.newContext();
  const page=await context.newPage();
  page.on('console',m=>{if(m.type()==='error')result.errors.push(m.text().replaceAll(invitation.secret,'[redacted]'));});
  // Fetch the genuine HTTPS document with normal TLS verification. Inject only this
  // test script into the disposable browser document; no server files are changed.
  await page.goto(origin,{waitUntil:'domcontentloaded',timeout:45000});
  result.actualOrigin=await page.evaluate(()=>location.origin);
  assert.equal(result.actualOrigin,origin);
  await page.setContent('<!doctype html><title>WebCam LAN feasibility fixture</title><h1>WebCam LAN feasibility fixture</h1><p>Synthetic media only; no camera or microphone access.</p><button id="run">Run synthetic test</button>');
  await page.click('#run');
  await page.evaluate(fs.readFileSync(path.join(root,'client.js'),'utf8')+'\nglobalThis.webcamTest=webcamFeasibility;');
  result.environment=await page.evaluate(()=>({secureContext:isSecureContext,mediaDevices:!!navigator.mediaDevices,
    webCrypto:!!crypto.subtle,userAgent:navigator.userAgent}));
  assert(result.environment.secureContext);
  // A default headless prompt is denied, distinct from a human permission test.
  result.defaultPermissionProbe=await page.evaluate(endpoint=>webcamTest.probe(endpoint),invitation.endpoint);
  result.checks.push('HTTPS origin loaded with normal certificate validation');
  const cdp=await context.newCDPSession(page);
  const {targetInfo}=await cdp.send('Target.getTargetInfo');
  await cdp.send('Browser.setPermission',{permission:{name:'local-network'},setting:'granted',origin,browserContextId:targetInfo.browserContextId});
  result.permissionMethod='CDP local-network permission grant in disposable test browser (not a human prompt test)';
  // A bad secret must close the connection without consuming the valid invitation.
  result.badSecretRejected=await page.evaluate(async invitation=>{
    const socket=new WebSocket(invitation.endpoint);
    await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=()=>reject(Error('LAN socket failed'));});
    const wrong={...invitation,secret:'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'};
    const envelope=await webcamTest.pack(wrong,{type:'offer',sdp:'invalid'});
    return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{socket.close();reject(Error('Bad-secret timeout'));},5000);
      socket.onclose=e=>{clearTimeout(timer);resolve(e.code===1008);};socket.send(JSON.stringify(envelope));});
  },invitation);
  assert(result.badSecretRejected);result.checks.push('wrong invitation secret rejected without consuming invitation');
  const media=await page.evaluate(invitation=>webcamTest.run(invitation),invitation);
  delete media.replayEnvelope;
  result.media=media;
  assert(media.received.videoFrames>0);assert(media.received.audioSamples>0);
  assert.equal(media.pair.remoteType,'host');assert.equal(media.pair.localType,'host');
  result.checks.push('encrypted offer/answer and ICE/DTLS/SRTP negotiation completed');
  result.checks.push('native Windows endpoint decoded synthetic video and audio');
  result.checks.push('selected ICE pair uses host candidates; no STUN/TURN configured');
  result.consumedInvitationProbe=await page.evaluate(endpoint=>webcamTest.probe(endpoint),invitation.endpoint);
  assert.equal(result.consumedInvitationProbe.opened,false);
  result.checks.push('consumed invitation rejected on a new connection');
  await context.close();
})().catch(error=>{result.failure=String(error);process.exitCode=1;}).finally(async()=>{
  if(browser)await browser.close();endpoint.kill();fs.closeSync(endpointLog);
  if(fs.existsSync(invitationFile))fs.unlinkSync(invitationFile);
  fs.writeFileSync(path.join(output,'browser-results.json'),JSON.stringify(result,null,2));
  console.log(JSON.stringify({checks:result.checks,failure:result.failure || null}));
});
