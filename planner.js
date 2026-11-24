/* Deterministic design model. No trained model or audio analysis is used. */
(function(root){
  const genres={Electronic:[50,65,65,60,-12], 'Hip-hop':[65,70,45,45,-12],Pop:[45,60,60,65,-12],Rock:[55,65,45,50,-13],Jazz:[55,30,45,45,-16],Ambient:[60,20,80,65,-16]};
  const clamp=(v,min,max)=>Math.max(min,Math.min(max,v));
  function plan(genre,prompt,mode='master'){
    const base=genres[genre]||genres.Electronic;
    const p={genre:genres[genre]?genre:'Electronic',prompt,mode,warmth:base[0],punch:base[1],width:base[2],air:base[3],lufs:base[4],ceiling:-1,enabled:{eq:true,dynamics:true,stereo:true,limiter:mode==='master'},notes:[]};
    const words=new Set(prompt.toLowerCase().match(/[a-z]+/g)||[]);
    const has=(...w)=>w.some(x=>words.has(x));
    if(has('warm','warmth','analog'))p.warmth+=15;
    if(has('bright','airy','air'))p.air+=15;
    if(has('dark','darker','mellow'))p.air-=25;
    if(has('punchy','punch','impact'))p.punch+=15;
    if(has('gentle','soft','natural'))p.punch-=20;
    if(has('wide','expansive','spacious'))p.width+=15;
    if(has('intimate','narrow','focused'))p.width-=20;
    for(const key of ['warmth','punch','width','air'])p[key]=clamp(p[key],0,100);
    if(has('bright','airy','air')&&has('dark','darker','mellow'))p.notes.push('Conflicting air directions were blended; adjust Air to choose.');
    const supported=new Set('warm warmth analog bright airy air dark darker mellow punchy punch impact gentle soft natural wide expansive spacious intimate narrow focused'.split(' '));
    if(![...words].some(w=>supported.has(w)))p.notes.push('No supported keywords matched; the genre starting point is shown.');
    if(has('not','no','without'))p.notes.push('This preview does not interpret negation; adjust the controls to clarify.');
    return p;
  }
  function values(p){return {low:+((p.warmth-50)*.04).toFixed(1),air:+((p.air-50)*.05).toFixed(1),ratio:+(1+p.punch*.02).toFixed(1),width:80+p.width*.4};}
  const api={plan,values,genres}; if(typeof module!=='undefined')module.exports=api;else root.IntentPlanner=api;
})(typeof globalThis!=='undefined'?globalThis:this);
