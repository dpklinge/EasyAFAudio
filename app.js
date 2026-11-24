const $=id=>document.getElementById(id);
let state,original=false;
const controls=['warmth','punch','width','air'];
function draw(){
  const p=original?{...state,warmth:50,punch:0,width:50,air:50}:state;
  const v=IntentPlanner.values(p);
  for(const k of controls){$(k).value=state[k];$(k+'-value').textContent=state[k]+'%';}
  $('lufs').value=state.lufs;$('ceiling').value=state.ceiling;
  const eqOn=!original&&state.enabled.eq;
  const points=[[30,85-(eqOn?v.low:0)*20],[190,85-(eqOn?v.low:0)*10],[350,85],[510,85-(eqOn?v.air:0)*10],[675,85-(eqOn?v.air:0)*20]];
  $('eq-line').setAttribute('d',points.map(([x,y],i)=>(i?'L':'M')+x+' '+y).join(' '));
  $('eq-points').innerHTML=points.map(([x,y])=>`<circle cx="${x}" cy="${y}" r="4"/>`).join('');
  const modules=[['eq','Tone EQ',`${eqOn?v.low:0} dB / ${eqOn?v.air:0} dB`,'Low shelf / high shelf'],['dynamics','Dynamics',original?'Bypassed':`${v.ratio}:1`,'Suggested compressor ratio'],['stereo','Stereo image',original?'100%':`${Math.round(v.width)}%`,'Proposed width · verify in mono'],['limiter',state.mode==='master'?'True-peak limiter':'Master-bus guard',state.mode==='master'?`${state.ceiling.toFixed(1)} dBTP`:'Reserved for master','Design target · not measured']];
  $('chain').replaceChildren(...modules.map(([key,name,value,desc])=>{const el=document.createElement('article');el.className='module'+(!state.enabled[key]||original?' off':'');const top=document.createElement('div');top.className='module-top';const label=document.createElement('label');const check=document.createElement('input');check.type='checkbox';check.checked=state.enabled[key];check.disabled=original||(state.mode==='mix'&&key==='limiter');check.setAttribute('aria-label','Enable '+name);check.onchange=()=>{state.enabled[key]=check.checked;draw();};label.append(check,document.createTextNode(name));top.append(label);const val=document.createElement('strong');val.textContent=value;const text=document.createElement('p');text.textContent=desc;el.append(top,val,text);return el;}));
  const adjectives=[state.warmth>60?'Warm':'Balanced',state.width>65?'expansive':state.width<40?'focused':'controlled'];
  $('direction').textContent=state.genre+' / '+adjectives.join(' & ');
  $('rationale').textContent=original?'Original settings view: all processing modules bypassed. This is a visual comparison only.':`${state.genre} starting point, shaped by your words. Every module remains editable. ${state.notes.join(' ')}`;
  $('compare').textContent=original?'Return to direction':'View original settings';$('compare').setAttribute('aria-pressed',String(original));
  $('session').replaceChildren();$('session').append(document.createTextNode(state.mode==='master'?'Stereo master bus':'Linked track instances (planned)'),document.createElement('br'));const small=document.createElement('small');small.textContent=state.mode==='master'?'Full mix in · shaped stereo out':'Separate tracks required · no stems connected';$('session').append(small);
  $('workflow-copy').textContent=state.mode==='master'?'Design targets for your stereo master. Final loudness needs a full-track measurement.':'Linked inserts shape individual tracks. These delivery targets are reserved for the final master bus.';
}
function generate(){state=IntentPlanner.plan($('genre').value,$('prompt').value,document.querySelector('[name=mode]:checked').value);original=false;draw();$('status').textContent='Direction updated. No audio has been analyzed.';}
$('generate').onclick=generate;
controls.forEach(k=>$(k).oninput=()=>{state[k]=Number($(k).value);original=false;draw();});
document.querySelectorAll('[name=mode]').forEach(el=>el.onchange=generate);
document.querySelectorAll('[data-word]').forEach(el=>el.onclick=()=>{const current=$('prompt').value;const next=current?current+', '+el.dataset.word:el.dataset.word;$('prompt').value=next.slice(0,500);generate();});
$('compare').onclick=()=>{original=!original;draw();};
for(const k of ['lufs','ceiling'])$(k).onchange=()=>{if(!$(k).checkValidity()||$(k).value===''){$('status').textContent='Enter a value within the displayed range.';draw();return;}state[k]=Number($(k).value);draw();};
$('reset').onclick=()=>{$('genre').value='Electronic';$('prompt').value='Warm low end, punchy drums, wide and airy';document.querySelector('[name=mode][value=master]').checked=true;generate();};
$('save').onclick=()=>{const blob=new Blob([JSON.stringify({schemaVersion:1,kind:'design-plan',engine:'deterministic-keyword-preview',audioAnalyzed:false,...state,derivedSuggestions:IntentPlanner.values(state)},null,2)],{type:'application/json'});const url=URL.createObjectURL(blob);const a=document.createElement('a');a.href=url;a.download='intent-processing-plan.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);$('status').textContent='Processing plan exported. No audio was rendered.';};
generate();
