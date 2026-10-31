/* Isolated HTTPS-origin test page. No production website or Movie integration. */
const webcamFeasibility = (() => {
  const protocol = 'djehuti-webcam-feasibility-v1';
  const encoder = new TextEncoder();
  const decode = s => Uint8Array.from(atob(s.replace(/-/g, '+').replace(/_/g, '/')), c => c.charCodeAt(0));
  const encode = bytes => btoa(String.fromCharCode(...new Uint8Array(bytes)))
    .replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
  async function derive(invitation, direction) {
    const key = await crypto.subtle.importKey('raw', decode(invitation.secret), 'HKDF', false, ['deriveKey']);
    return crypto.subtle.deriveKey({name:'HKDF', hash:'SHA-256', salt:encoder.encode(invitation.session),
      info:encoder.encode(`${protocol}:${direction}`)}, key, {name:'AES-GCM', length:256}, false, ['encrypt','decrypt']);
  }
  async function pack(invitation, offer) {
    const nonce = crypto.getRandomValues(new Uint8Array(12));
    const ciphertext = await crypto.subtle.encrypt({name:'AES-GCM', iv:nonce,
      additionalData:encoder.encode(invitation.session)}, await derive(invitation, 'offer'), encoder.encode(JSON.stringify(offer)));
    return {session:invitation.session, nonce:encode(nonce), ciphertext:encode(ciphertext)};
  }
  async function unpack(invitation, envelope) {
    if (envelope.session !== invitation.session) throw Error('Wrong session');
    const plaintext = await crypto.subtle.decrypt({name:'AES-GCM', iv:decode(envelope.nonce),
      additionalData:encoder.encode(invitation.session)}, await derive(invitation, 'answer'), decode(envelope.ciphertext));
    return JSON.parse(new TextDecoder().decode(plaintext));
  }
  function deadline(promise, milliseconds, label) {
    let timer;
    return Promise.race([promise, new Promise((_, reject) => {timer=setTimeout(() => reject(Error(label)), milliseconds);})])
      .finally(() => clearTimeout(timer));
  }
  async function probe(endpoint) {
    const socket = new WebSocket(endpoint);
    try {
      await deadline(new Promise((resolve,reject) => {socket.onopen=resolve;socket.onerror=() => reject(Error('WebSocket blocked or unreachable'));}), 8000, 'WebSocket timeout');
      return {opened:true};
    } catch(error) { return {opened:false,error:String(error)}; }
    finally {socket.close();}
  }
  async function run(invitation, milliseconds=5000) {
    if (!isSecureContext) throw Error('Secure HTTPS origin required');
    const started = performance.now();
    const pc = new RTCPeerConnection({iceServers:[]});
    const dc = pc.createDataChannel('feasibility');
    const socket = new WebSocket(invitation.endpoint);
    const canvas = document.createElement('canvas'); canvas.width=320; canvas.height=240;
    const ctx = canvas.getContext('2d');
    let tick=0;
    const interval = setInterval(() => {ctx.fillStyle=`hsl(${tick++*7%360} 80% 50%)`;ctx.fillRect(0,0,320,240);}, 33);
    const video = canvas.captureStream(15);
    const audio = new AudioContext();
    const oscillator = audio.createOscillator(); const gain=audio.createGain(); gain.gain.value=0.05;
    const destination = audio.createMediaStreamDestination(); oscillator.connect(gain).connect(destination); oscillator.start();
    let answerSeen=false;
    try {
      for(const track of [...video.getTracks(), ...destination.stream.getTracks()]) pc.addTrack(track);
      const opened = new Promise((resolve,reject) => {socket.onopen=resolve;socket.onerror=() => reject(Error('WebSocket blocked or unreachable'));});
      await deadline(opened,8000,'WebSocket timeout');
      const answer = new Promise((resolve,reject) => {socket.onmessage=e => resolve(JSON.parse(e.data));socket.onclose=() => {if(!answerSeen)reject(Error('Signaling closed'));};});
      await pc.setLocalDescription(await pc.createOffer());
      if(pc.iceGatheringState!=='complete') await deadline(new Promise(resolve => {pc.onicegatheringstatechange=()=>{if(pc.iceGatheringState==='complete')resolve();};}), 10000, 'ICE gathering timeout');
      const envelope = await pack(invitation, {type:'offer',sdp:pc.localDescription.sdp});
      socket.send(JSON.stringify(envelope));
      const reply = await deadline(answer,15000,'Answer timeout'); answerSeen=true;
      await pc.setRemoteDescription(await unpack(invitation,reply));
      await deadline(new Promise((resolve,reject) => {
        if(dc.readyState==='open')return resolve();dc.onopen=resolve;
        pc.onconnectionstatechange=()=>{if(pc.connectionState==='failed')reject(Error('ICE/DTLS failed'));};
      }),15000,'Data channel timeout');
      const connectedMs = performance.now()-started;
      await new Promise(resolve=>setTimeout(resolve,milliseconds));
      const counts = new Promise(resolve=>{dc.onmessage=e=>resolve(JSON.parse(e.data));});dc.send('stats');
      const received=await deadline(counts,3000,'Receiver stats timeout');
      const stats=await pc.getStats();let selectedPair, local, remote;const outbound=[];
      stats.forEach(row=>{if(row.type==='transport'&&row.selectedCandidatePairId)selectedPair=stats.get(row.selectedCandidatePairId);
        if(row.type==='outbound-rtp')outbound.push({kind:row.kind,codec:stats.get(row.codecId)?.mimeType,
          packetsSent:row.packetsSent,bytesSent:row.bytesSent});});
      if(selectedPair){local=stats.get(selectedPair.localCandidateId);remote=stats.get(selectedPair.remoteCandidateId);}
      return {secureContext:isSecureContext,origin:location.origin,userAgent:navigator.userAgent,connectedMs,received,outbound,
        pair:selectedPair?{state:selectedPair.state,localType:local?.candidateType,remoteType:remote?.candidateType,
          localProtocol:local?.protocol,remoteProtocol:remote?.protocol}:null,
        // Retained only in memory for replay tests, not written into results.
        replayEnvelope:envelope};
    } finally {
      clearInterval(interval);video.getTracks().forEach(t=>t.stop());destination.stream.getTracks().forEach(t=>t.stop());
      oscillator.stop();await audio.close();pc.close();socket.close();
    }
  }
  return {probe,run,pack,unpack};
})();
