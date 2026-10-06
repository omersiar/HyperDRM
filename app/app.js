'use strict';
var pending=[];
var targetDirty=false,receiver='Hyperion';
var profileDirty=false,profile='auto';
var profileNames={auto:'Auto',g4:'LG G4 (measured)',average:'Average (uniform)',custom:'Custom model'};
function showProfile(){document.getElementById('profile').textContent='Profile: '+profileNames[profile];document.getElementById('customEditor').style.display=profile==='custom'?'block':'none';document.getElementById('customProfile').disabled=profile!=='custom';}
function call(method,done,data){
 if(typeof PalmServiceBridge==='undefined'){done({returnValue:false,errorText:'This app requires an LG TV with the native service installed.'});return;}
 var bridge=new PalmServiceBridge(),timer,finished=false;
 function finish(value){if(finished)return;finished=true;clearTimeout(timer);var i=pending.indexOf(bridge);if(i>=0)pending.splice(i,1);done(value);}
 bridge.onservicecallback=function(raw){try{finish(JSON.parse(raw));}catch(e){finish({returnValue:false,errorText:'Invalid service response'});}};
 pending.push(bridge);timer=setTimeout(function(){bridge.cancel();finish({returnValue:false,errorText:'The service is not responding. Check the installation and service log.'});},10000);
 bridge.call('luna://org.webosbrew.histogramhyperion.service/'+method,JSON.stringify(data||{}));
}
function display(result){
 if(result.calibrationProfile && !profileDirty){
  profile=result.calibrationProfile;showProfile();
  document.getElementById('customProfile').value=JSON.stringify({model:result.customModel||result.tvModel||'TV-MODEL',points:result.customPoints&&result.customPoints.length?result.customPoints:[]},null,2);
 }
 if(result.effectiveCalibration)document.getElementById('profileStatus').textContent='TV: '+(result.tvModel||'Unknown')+' · Active profile: '+profileNames[result.effectiveCalibration]+(result.effectiveCalibration==='average'?' · Spatial calibration needed for this model':'');

 if(result.host && !targetDirty){
  document.getElementById('host').value=result.host;document.getElementById('port').value=result.port;document.getElementById('piccapPort').value=result.piccapPort||19400;receiver=result.receiver;
  document.getElementById('receiver').textContent='Receiver: '+receiver;
 }
 document.getElementById('token').placeholder=result.tokenSet?'Saved token (leave blank to keep)':'No token saved';
 if(result.endpoint)document.getElementById('endpoint').textContent=result.receiver+' · '+result.endpoint+' · Priority 149 · 10 updates per second';
 var t=result.connectionTest;
 if(t)document.getElementById('testResult').textContent='Connection test: '+t.state+(t.state==='Passed'?' · '+t.ledCount+' LEDs · '+t.elapsedMs+' ms':'')+(t.error?' · '+t.error:'');
 document.getElementById('testConnection').disabled=!!t && ['Connecting','Sending request','Checking authentication','Reading LED layout'].indexOf(t.state)>=0;
 if(result.version)document.getElementById('diagnostics').textContent='Version '+result.version+' · '+(result.uid===0?'Root service':'Service needs root elevation')+' · '+result.captureState+' · Playback: '+result.playbackProtection+' · Capture recoveries: '+result.captureRecoveries+' (last: '+result.lastRecoveryMs+' ms)'+(result.lastOutputAgeMs>=0?' · Last histogram output: '+result.lastOutputAgeMs+' ms ago':'')+(result.retryScheduled?' · Retry in '+result.retryInMs+' ms · Attempts: '+result.retryAttempts:'')+(result.piccapError?' · '+result.piccapError:'')+(result.piccapCooldownMs?' · PicCap retry in '+Math.ceil(result.piccapCooldownMs/1000)+' s':'')+' · Layout updates: '+(result.layoutUpdates||0)+(result.layoutError?' · Layout: '+result.layoutError:'');
 var handover=document.getElementById('handover');handover.disabled=!result.piccapInstalled;
 handover.setAttribute('aria-checked',String(!!result.piccapHandover));
 handover.textContent=result.piccapInstalled?'Auto PicCap: '+(result.piccapHandover?'On':'Off'):'Auto PicCap: Not installed';
 if(typeof result.autostart==='boolean'){var toggle=document.getElementById('autostart');toggle.setAttribute('aria-checked',String(result.autostart));toggle.textContent='Autostart: '+(result.autostart?'On':'Off');}
 document.getElementById('error').textContent=result.errorText||result.error||'';
 document.getElementById('status').textContent=result.returnValue===false?'Service unavailable':(result.running?'Running':'Stopped')+' · '+(result.frames||0)+' acknowledged color updates';
 if(result.activeSource==='PicCap'){
  document.getElementById('status').textContent='PicCap · '+(result.running?'Running':'Switching capture source')+' · '+(result.piccapConnected?'Receiver connected':'Waiting for receiver');
  document.getElementById('regions').textContent='';return;
 }
 if(result.piccapHandover)document.getElementById('status').textContent+=' · HyperDRM · '+result.playbackProtection;
 if(result.piccapHandover && !result.piccapTargetCompatible)document.getElementById('status').textContent+=' · Auto PicCap unavailable for remote IPv6 target';
 if(result.colorValid && result.effectiveCalibration==='average')document.getElementById('status').textContent+=' · Average color output';
 if(result.colorValid)document.getElementById('status').textContent+=result.spatialOutput?' · '+result.ledCount+' LEDs from 16 color regions'+(result.receiver==='HyperHDR'?' · Spatial image output':''):' · Output RGB: '+result.outputColor.join(', ');
 else if(result.returnValue!==false)document.getElementById('status').textContent+=' · No current color output';
 if(result.blackBarDetection && result.running)document.getElementById('status').textContent+=result.letterboxDetected?' · Top/bottom black bars detected':' · Full-screen capture';
 if(result.spatialInterpolation)document.getElementById('status').textContent+=' · Smooth color transitions';
 if(result.retryScheduled)document.getElementById('status').textContent+=' · Retrying receiver or capture startup';
 if(result.retryPaused)document.getElementById('status').textContent+=' · Automatic retry paused until Start';
 var box=document.getElementById('regions');box.textContent='';
 (result.regions||[]).forEach(function(rgb,index){var e=document.createElement('div');e.className='swatch';e.style.backgroundColor='rgb('+rgb.join(',')+')';e.title='Histogram '+index;box.appendChild(e);});
}
document.getElementById('start').onclick=function(){call('start',function(r){if(r.returnValue===false)display(r);else call('status',display);});};
document.getElementById('stop').onclick=function(){call('stop',function(){call('status',display);});};
document.getElementById('refresh').onclick=function(){call('status',display);};
call('status',display);setInterval(function(){if(!pending.length)call('status',display);},3000);
document.getElementById('start').focus();

document.getElementById('autostart').onclick=function(){var button=this;var enabled=button.getAttribute('aria-checked')!=='true';button.disabled=true;call('autostart',function(r){button.disabled=false;if(r.returnValue===false)document.getElementById('error').textContent=r.errorText;else call('status',display);},{enabled:enabled});};

document.getElementById('handover').onclick=function(){var button=this;button.disabled=true;call('handover',function(r){button.disabled=false;if(r.returnValue===false)document.getElementById('error').textContent=r.errorText;else call('status',display);},{enabled:button.getAttribute('aria-checked')!=='true'});};
['host','port','piccapPort','token','clearToken'].forEach(function(id){document.getElementById(id).addEventListener('input',function(){targetDirty=true;});});
document.getElementById('receiver').onclick=function(){receiver=receiver==='Hyperion'?'HyperHDR':'Hyperion';this.textContent='Receiver: '+receiver;targetDirty=true;};
document.getElementById('saveTarget').onclick=function(){
 var port=Number(document.getElementById('port').value),button=this;
 if(!Number.isInteger(port)||port<1||port>65535){document.getElementById('error').textContent='JSON port must be an integer from 1 to 65535.';return;}
 var flatPort=Number(document.getElementById('piccapPort').value);
 if(!Number.isInteger(flatPort)||flatPort<1||flatPort>65535){document.getElementById('error').textContent='PicCap FlatBuffer port must be an integer from 1 to 65535.';return;}
 var settings={piccapPort:flatPort,host:document.getElementById('host').value.trim(),port:port,receiver:receiver,clearToken:document.getElementById('clearToken').checked};
 var token=document.getElementById('token').value.trim();if(token)settings.token=token;
 button.disabled=true;call('settings',function(r){button.disabled=false;if(r.returnValue===false){document.getElementById('error').textContent=r.errorText;return;}targetDirty=false;document.getElementById('token').value='';document.getElementById('clearToken').checked=false;call('status',display);},settings);
};
document.getElementById('testConnection').onclick=function(){if(targetDirty){document.getElementById('error').textContent='Save receiver settings before testing the connection.';return;}this.disabled=true;call('testConnection',function(r){if(r.returnValue===false)document.getElementById('error').textContent=r.errorText;call('status',display);});};
document.getElementById('profile').onclick=function(){var keys=['auto','g4','average','custom'];profile=keys[(keys.indexOf(profile)+1)%keys.length];profileDirty=true;showProfile();};
document.getElementById('customProfile').addEventListener('input',function(){profileDirty=true;});
document.getElementById('saveProfile').onclick=function(){var data={profile:profile},button=this;if(profile==='custom'){try{var parsed=JSON.parse(document.getElementById('customProfile').value);data.model=parsed.model;data.points=parsed.points;}catch(e){document.getElementById('error').textContent='Custom profile must be valid JSON.';return;}}button.disabled=true;call('calibration',function(r){button.disabled=false;if(r.returnValue===false){document.getElementById('error').textContent=r.errorText;return;}profileDirty=false;call('status',display);},data);};
setupRemoteNavigation(['start','stop','refresh','autostart','handover','receiver','host','port','piccapPort','token','clearToken','saveTarget','testConnection','profile','customProfile','saveProfile'].map(function(id){return document.getElementById(id);}),document,window);
