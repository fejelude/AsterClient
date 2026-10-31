import { world, system, ItemStack } from '@minecraft/server';

// The native client never depends on this pack. It is a separate observer/test fixture.
const prefix = 'AsterQA:';
const suites={
  aim:[
    'baseline','horizontal','baseline','vertical','baseline','both','moving-gate','combat-idle','combat-held','players-only','wall-blocked','wall-ignored','on-target','blatant','off'
  ].map((label,i)=>({at:i*4,label,player:{x:1001.5,y:-60,z:995},yaw:label==='blatant'?60:0,pitch:0,wall:label==='wall-blocked'||label==='wall-ignored'})),
  targeting:[
    ['baseline',1000.5,995.5],['reach-3',1000.5,995.5],['reach-5.2',1000.5,995.5],['moving-gate',1000.5,995.5],['combat-idle',1000.5,995.5],['combat-held',1000.5,995.5],
    ['box-baseline',1001.3,998],['box-2.22',1001.3,998],['box-0.4',1001.3,998],['combined',1001.3,995.5],['occluded',1001.3,995.5],['off',1001.3,995.5]
  ].map(([label,x,z],i)=>({at:i*5,label,player:{x,y:-60,z},yaw:0,pitch:0,wall:label==='occluded'}))
};
let player, target, anchor = {x:1000.5,y:-60,z:1000.5};
let counters = {hits:0,hurt:0,damage:0,broken:0,placed:0};
let recording = false, remaining = 0, name = '', events = [], measurements = [];
let clock = {tick:0,wall:Date.now(),tps:0};
let blockFixture=null;
let seriesGeneration=0;
let occlusion=[];
function setOcclusion(enabled){
  for(const pos of occlusion){const b=player.dimension.getBlock(pos);if(b?.typeId==='minecraft:stone')b.setType('minecraft:air');}
  occlusion=[];
  if(!enabled)return;
  const positions=[];
  for(let x=999;x<=1002;++x)for(let y=-60;y<=-57;++y)positions.push({x,y,z:998});
  if(positions.some(pos=>player.dimension.getBlock(pos)?.typeId!=='minecraft:air'))throw new Error('Occlusion fixture space is occupied.');
  for(const pos of positions)player.dimension.getBlock(pos).setType('minecraft:stone');
  occlusion=positions;
}
function cleanupBlocks(){
  if(!blockFixture||!valid(player))return;
  for(const pos of blockFixture.blocks){const block=player.dimension.getBlock(pos);if(block?.typeId===blockFixture.block)block.setType('minecraft:air');}
  const inventory=player.getComponent('minecraft:inventory')?.container;
  if(inventory?.getItem(blockFixture.slot)?.nameTag==='Aster QA fixture block')inventory.setItem(blockFixture.slot,undefined);
  blockFixture=null;
}
function log(type, data={}) {
  const item = {type,tick:system.currentTick,wall:Date.now(),name,...data};
  if(recording && events.length<2000) events.push(item);
  console.warn('[AsterQA] '+JSON.stringify(item));
}
function valid(e) {try{return !!e && e.isValid;}catch{return false;}}
function health(e) {try{return e.getComponent('minecraft:health')?.currentValue;}catch{return null;}}
function reset(data={}) {
  ++seriesGeneration;
  if(!valid(player)) throw new Error('Run this script event as the test player.');
  anchor={x:data.x??1000.5,y:-60,z:data.z??1000.5};
  if(!valid(target)) {
    target=player.dimension.getEntities({name:prefix+'Target'})[0]??player.dimension.spawnEntity('minecraft:villager_v2',anchor);
    target.nameTag=prefix+'Target';
    target.addEffect('slowness',200000,{amplifier:255,showParticles:false});
    try{target.getComponent('minecraft:movement')?.setCurrentValue(0);}catch{}
  }
  target.clearVelocity();target.teleport(anchor);
  target.getComponent('minecraft:health')?.resetToMaxValue();
  const p=data.player??{x:1001.5,y:-60,z:995};
  player.clearVelocity();player.teleport(p,{rotation:{x:data.pitch??0,y:data.yaw??0}});
  counters={hits:0,hurt:0,damage:0,broken:0,placed:0};
  log('reset',{anchor,player:p});
}
function finish() {
  if(!recording)return;
  log('summary',{name,counters,tps:clock.tps,samples:measurements.length});
  recording=false;
  const result={name,counters,events,measurements};
  // Small chunks survive in this task-owned world's dynamic properties for later inspection.
  const serialized=JSON.stringify(result);
  const count=Math.ceil(serialized.length/30000);
  world.setDynamicProperty('asterqa:reportCount',count);
  for(let i=0;i<count;i++)world.setDynamicProperty('asterqa:report'+i,serialized.slice(i*30000,(i+1)*30000));
  world.sendMessage(`Aster QA ${name}: hits=${counters.hits} hurt=${counters.hurt} damage=${counters.damage} broken=${counters.broken} placed=${counters.placed} serverTPS=${clock.tps.toFixed(2)}`);
}
world.afterEvents.entityHitEntity.subscribe(e=>{
  if(e.hitEntity.nameTag!==prefix+'Target')return;
  ++counters.hits;
  log('accepted-entity-hit',{player:e.damagingEntity.location,target:e.hitEntity.location,rotation:e.damagingEntity.getRotation()});
});
world.afterEvents.entityHurt.subscribe(e=>{
  if(e.hurtEntity.nameTag===prefix+'Target') {
    ++counters.hurt;counters.damage+=e.damage;
    log('accepted-damage',{damage:e.damage,health:health(e.hurtEntity),location:e.hurtEntity.location});
    // Reset only the owned fixture, on the following authoritative tick.
    system.run(()=>{if(valid(target)){target.clearVelocity();target.teleport(anchor);target.getComponent('minecraft:health')?.resetToMaxValue();}});
  } else if(valid(player)&&e.hurtEntity.id===player.id)log('player-hurt',{damage:e.damage,velocity:player.getVelocity()});
});
world.afterEvents.playerBreakBlock.subscribe(e=>{if(valid(player)&&e.player.id===player.id){++counters.broken;log('block-broken',{position:e.block.location,type:e.brokenBlockPermutation.type.id});}});
world.afterEvents.playerPlaceBlock.subscribe(e=>{if(valid(player)&&e.player.id===player.id){++counters.placed;log('block-placed',{position:e.block.location,type:e.block.typeId});if(blockFixture?.placing&&e.block.location.x===1003&&e.block.location.y===-59&&e.block.location.z>=999&&e.block.location.z<=1000){const pos=e.block.location;system.run(()=>{const block=player.dimension.getBlock(pos);if(block?.typeId==='minecraft:dirt')block.setType('minecraft:air');});}}});
system.afterEvents.scriptEventReceive.subscribe(e=>{
  if(!e.id.startsWith('aster:'))return;
  system.run(()=>{
    try {
      if(e.sourceEntity?.typeId==='minecraft:player')player=e.sourceEntity;
      const data=e.message?JSON.parse(e.message):{};
      if(e.id==='aster:setup'||e.id==='aster:reset')reset(data);
      if(e.id==='aster:heal'){
        player.getComponent('minecraft:health')?.resetToMaxValue();
        player.addEffect('saturation',40,{amplifier:255,showParticles:false});
        log('player-restored');
      }
      if(e.id==='aster:trial') {finish();reset(data);name=data.name??'trial';events=[];measurements=[];remaining=data.ticks??400;recording=true;log('start',{name,counters});}
      if(e.id==='aster:record') {finish();name=data.name??'trial';events=[];measurements=[];remaining=data.ticks??240;recording=true;log('start',{name,counters});}
      if(e.id==='aster:sequence'){
        finish();reset(data);name=data.name??'sequence';events=[];measurements=[];remaining=data.ticks??1800;recording=true;log('start',{name,counters});
        const stages=data.suite?suites[data.suite]:data.stages;if(!stages)throw new Error('Unknown QA suite.');
        let index=0,token=seriesGeneration;
        const poll=system.runInterval(()=>{
          if(token!==seriesGeneration||index>=stages.length){system.clearRun(poll);return;}
          const stage=stages[index],due=data.startAt+stage.at*1000;
          if(Date.now()<due)return;
          if(stage.wall!==undefined)setOcclusion(stage.wall);
          reset(stage);token=seriesGeneration;
          log('phase',{name,index,label:stage.label,lateness:Date.now()-due});++index;
        },1);
      }
      if(e.id==='aster:knock') {
        player.clearVelocity();player.applyDamage(1,{cause:'entityAttack',damagingEntity:target});
        player.applyKnockback({x:data.x??0,z:data.z??-.4},data.y??.4);
        log('controlled-knockback',{input:data,velocity:player.getVelocity()});
      }
      if(e.id==='aster:knockSeries') {
        finish();reset(data);name=data.name??'knock-series';events=[];measurements=[];remaining=data.ticks??700;recording=true;log('start',{name,counters});
        const trials=Math.min(60,data.trials??8),period=data.period??60,start=data.start??80,token=seriesGeneration;
        function knock(i){
          if(token!==seriesGeneration||!valid(player)||!valid(target))return;
          player.clearVelocity();player.teleport(data.player??{x:1000.5,y:-60,z:995},{rotation:{x:0,y:0}});
          player.getComponent('minecraft:health')?.resetToMaxValue();
          player.applyDamage(1,{cause:'entityAttack',damagingEntity:target});
          player.applyKnockback({x:data.xImpulse??.25,z:data.zImpulse??-.4},data.yImpulse??.4);
          log('controlled-knockback',{trial:i,name,input:{x:data.xImpulse??.25,z:data.zImpulse??-.4,y:data.yImpulse??.4},velocity:player.getVelocity()});
        }
        if(data.startAt){
          let next=0;const groupSize=data.groupSize??10,phaseMs=data.phaseMs??25000,periodMs=data.periodMs??2000;
          const poll=system.runInterval(()=>{
            if(token!==seriesGeneration||next>=trials){system.clearRun(poll);return;}
            const due=data.startAt+Math.floor(next/groupSize)*phaseMs+(next%groupSize)*periodMs+1000;
            if(Date.now()>=due){if(Date.now()-due<1000)knock(next);else log('skipped-trial',{trial:next,lateness:Date.now()-due});++next;}
          },1);
        }else for(let i=0;i<trials;i++)system.runTimeout(()=>knock(i),start+i*period);
      }
      if(e.id==='aster:stop')finish();
      if(e.id==='aster:blocks'){
        cleanupBlocks();finish();reset({...data,player:{x:1003.5,y:-60,z:998}});
        const slot=player.selectedSlotIndex,inventory=player.getComponent('minecraft:inventory')?.container;
        if(inventory.getItem(slot))throw new Error('Select an empty slot before block fixture setup.');
        const pos={x:1003,y:-59,z:1001},block=player.dimension.getBlock(pos);
        if(block?.typeId!=='minecraft:air')throw new Error('Fixture space is occupied.');
        const type=data.placing?'minecraft:stone':(data.block??'minecraft:dirt');block.setType(type);
        blockFixture={slot,blocks:[pos],block:type,placing:!!data.placing};
        if(data.placing){const item=new ItemStack('minecraft:dirt',64);item.nameTag='Aster QA fixture block';inventory.setItem(slot,item);}
        name=data.name??'block-fixture';events=[];measurements=[];remaining=data.ticks??500;recording=true;log('start',{name,counters});
      }
      if(e.id==='aster:cleanup'){++seriesGeneration;finish();cleanupBlocks();setOcclusion(false);}
      if(e.id==='aster:status')world.sendMessage('Aster QA '+JSON.stringify(counters));
    }catch(error){world.sendMessage('Aster QA error: '+error);console.warn('[AsterQA] '+String(error));}
  });
});
system.runInterval(()=>{
  try {
    if(system.currentTick-clock.tick>=20){const now=Date.now();clock.tps=(system.currentTick-clock.tick)*1000/(now-clock.wall);clock.tick=system.currentTick;clock.wall=now;}
    if(valid(player)) {
      player.onScreenDisplay.setActionBar(`ASTER QA | hits ${counters.hits} | damage ${counters.damage} | blocks ${counters.broken}/${counters.placed} | server ${clock.tps.toFixed(1)} TPS`);
      if(recording){const measurement={tick:system.currentTick,wall:Date.now(),position:player.location,velocity:player.getVelocity(),rotation:player.getRotation(),target:valid(target)?target.location:null,health:health(target)};measurements.push(measurement);console.warn('[AsterQA] '+JSON.stringify({type:'sample',name,...measurement}));if(--remaining<=0)finish();}
    }
  }catch(error){console.warn('[AsterQA] tick: '+String(error));}
},1);
system.run(()=>log('pack-loaded'));
