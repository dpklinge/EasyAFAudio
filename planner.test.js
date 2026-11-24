const assert=require('node:assert/strict');
const {plan,values,genres}=require('./planner.js');
const basic=plan('Electronic','');
assert.equal(basic.lufs,-12);
assert.ok(plan('Electronic','warm').warmth>basic.warmth);
assert.ok(plan('Electronic','punchy').punch>basic.punch);
assert.ok(plan('Electronic','dark').air<basic.air);
assert.ok(plan('Electronic','intimate').width<basic.width);
assert.equal(plan('Electronic','swarm').warmth,basic.warmth,'Only match complete keywords');
assert.ok(plan('Electronic','bright and dark').notes.some(s=>s.includes('Conflicting')));
assert.ok(plan('Electronic','not bright').notes.some(s=>s.includes('negation')));
assert.ok(plan('Electronic','cinematic underwater').notes.some(s=>s.includes('No supported')));
assert.equal(plan('Unknown','').genre,'Electronic');
assert.equal(plan('Pop','', 'mix').enabled.limiter,false);
assert.equal(plan('Pop','', 'master').enabled.limiter,true);
for(const genre of Object.keys(genres))for(const prompt of ['','warm punchy wide airy','dark gentle narrow','warm '.repeat(100)]){
  const p=plan(genre,prompt);for(const k of ['warmth','punch','width','air'])assert.ok(p[k]>=0&&p[k]<=100);
  const v=values(p);assert.ok(Object.values(v).every(Number.isFinite));
  assert.ok(v.ratio>=1&&v.ratio<=3);assert.ok(v.width>=80&&v.width<=120);
}
console.log('Planner checks passed: keyword behavior, conflicts, bounds, finite parameters, and workflow defaults.');
