'use strict';
var pending=[];
function call(method,done,data){
 if(typeof PalmServiceBridge==='undefined'){done({returnValue:false,errorText:'This app requires an LG TV with the native service installed.'});return;}
 var bridge=new PalmServiceBridge(),timer,finished=false;
 function finish(value){if(finished)return;finished=true;clearTimeout(timer);var i=pending.indexOf(bridge);if(i>=0)pending.splice(i,1);done(value);}
 bridge.onservicecallback=function(raw){try{finish(JSON.parse(raw));}catch(e){finish({returnValue:false,errorText:'Invalid service response'});}};
 pending.push(bridge);timer=setTimeout(function(){bridge.cancel();finish({returnValue:false,errorText:'The service is not responding. Check the installation and service log.'});},10000);
 bridge.call('luna://org.webosbrew.histogramhyperion.service/'+method,JSON.stringify(data||{}));
}
function display(result){
 if(typeof result.autostart==='boolean'){var toggle=document.getElementById('autostart');toggle.setAttribute('aria-checked',String(result.autostart));toggle.textContent='Autostart: '+(result.autostart?'On':'Off');}
 document.getElementById('error').textContent=result.errorText||result.error||'';
 document.getElementById('status').textContent=result.returnValue===false?'Service unavailable':(result.running?'Running':'Stopped')+' · '+(result.frames||0)+' acknowledged color updates';
 if(result.colorValid)document.getElementById('status').textContent+=result.spatialOutput?' · '+result.ledCount+' LEDs from 16 color regions':' · Output RGB: '+result.outputColor.join(', ');
 else if(result.returnValue!==false)document.getElementById('status').textContent+=' · No current color output';
 if(result.blackBarDetection && result.running)document.getElementById('status').textContent+=result.letterboxDetected?' · Top/bottom black bars detected':' · Full-screen capture';
 if(result.spatialInterpolation)document.getElementById('status').textContent+=' · Smooth color transitions';
 var box=document.getElementById('regions');box.textContent='';
 (result.regions||[]).forEach(function(rgb,index){var e=document.createElement('div');e.className='swatch';e.style.backgroundColor='rgb('+rgb.join(',')+')';e.title='Histogram '+index;box.appendChild(e);});
}
document.getElementById('start').onclick=function(){call('start',function(r){if(r.returnValue===false)display(r);else call('status',display);});};
document.getElementById('stop').onclick=function(){call('stop',function(){call('status',display);});};
document.getElementById('refresh').onclick=function(){call('status',display);};
call('status',display);setInterval(function(){if(!pending.length)call('status',display);},3000);
document.getElementById('start').focus();

document.getElementById('autostart').onclick=function(){var button=this;var enabled=button.getAttribute('aria-checked')!=='true';button.disabled=true;call('autostart',function(r){button.disabled=false;if(r.returnValue===false)document.getElementById('error').textContent=r.errorText;else call('status',display);},{enabled:enabled});};
