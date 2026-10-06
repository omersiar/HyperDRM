'use strict';
var pending=[];
function call(method,done,data){
 if(typeof PalmServiceBridge==='undefined'){done({returnValue:false,errorText:'Diese App benötigt einen LG-TV mit installiertem nativem Dienst.'});return;}
 var bridge=new PalmServiceBridge(),timer,finished=false;
 function finish(value){if(finished)return;finished=true;clearTimeout(timer);var i=pending.indexOf(bridge);if(i>=0)pending.splice(i,1);done(value);}
 bridge.onservicecallback=function(raw){try{finish(JSON.parse(raw));}catch(e){finish({returnValue:false,errorText:'Ungültige Antwort vom Dienst'});}};
 pending.push(bridge);timer=setTimeout(function(){bridge.cancel();finish({returnValue:false,errorText:'Der Dienst antwortet nicht. Installation und Dienstprotokoll prüfen.'});},10000);
 bridge.call('luna://org.webosbrew.histogramhyperion.service/'+method,JSON.stringify(data||{}));
}
function display(result){
 if(typeof result.autostart==='boolean'){var toggle=document.getElementById('autostart');toggle.setAttribute('aria-checked',String(result.autostart));toggle.textContent='Autostart: '+(result.autostart?'Ein':'Aus');}
 document.getElementById('error').textContent=result.errorText||result.error||'';
 document.getElementById('status').textContent=result.returnValue===false?'Dienst nicht bereit':(result.running?'Aktiv':'Gestoppt')+' · '+(result.frames||0)+' bestätigte Farbausgaben';
 if(result.colorValid)document.getElementById('status').textContent+=result.spatialOutput?' · '+result.ledCount+' LEDs aus 16 Farbbereichen':' · Gesendetes RGB: '+result.outputColor.join(', ');
 else if(result.returnValue!==false)document.getElementById('status').textContent+=' · Keine aktuelle Farbausgabe';
 if(result.blackBarDetection && result.running)document.getElementById('status').textContent+=result.letterboxDetected?' · Balken oben/unten erkannt':' · Vollbild-Erfassung';
 if(result.spatialInterpolation)document.getElementById('status').textContent+=' · Feine Farbübergänge';
 var box=document.getElementById('regions');box.textContent='';
 (result.regions||[]).forEach(function(rgb,index){var e=document.createElement('div');e.className='swatch';e.style.backgroundColor='rgb('+rgb.join(',')+')';e.title='Histogramm '+index;box.appendChild(e);});
}
document.getElementById('start').onclick=function(){call('start',function(r){if(r.returnValue===false)display(r);else call('status',display);});};
document.getElementById('stop').onclick=function(){call('stop',function(){call('status',display);});};
document.getElementById('refresh').onclick=function(){call('status',display);};
call('status',display);setInterval(function(){if(!pending.length)call('status',display);},3000);
document.getElementById('start').focus();

document.getElementById('autostart').onclick=function(){var button=this;var enabled=button.getAttribute('aria-checked')!=='true';button.disabled=true;call('autostart',function(r){button.disabled=false;if(r.returnValue===false)document.getElementById('error').textContent=r.errorText;else call('status',display);},{enabled:enabled});};
