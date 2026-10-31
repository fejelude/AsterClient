"""Create reproducible native scenarios for the optional disposable-world QA pack."""
import argparse
import json
from pathlib import Path
import time

def aim():
    labels = ['baseline','horizontal','baseline','vertical','baseline','both','moving-gate',
              'combat-idle','combat-held','players-only','wall-blocked','wall-ignored','on-target','blatant','off']
    stages = []
    for i, label in enumerate(labels):
        settings = dict(distance=10, smoothness=25, fovHorizontal=.4, fovVertical=.02, height=-.3,
            horizontal=label!='vertical', vertical=label!='horizontal', antibot=label=='players-only',
            onlyMoving=label=='moving-gate', avoidWalls=label!='wall-ignored', avoidOnTarget=label=='on-target',
            cacheTargets=True, combatMode=label in ('combat-idle','combat-held'))
        if label=='blatant':settings.update(smoothness=5, fovHorizontal=3.14, fovVertical=0)
        stages.append(dict(at=i*4, modules=dict(aim=dict(enabled=label not in ('baseline','off'),
            profile='blatant' if label=='blatant' else 'closet', settings=settings)), mouse=[label=='combat-held',False]))
    return 60, stages

def targeting():
    labels=['baseline','reach-3','reach-5.2','moving-gate','combat-idle','combat-held',
            'box-baseline','box-2.22','box-0.4','combined','occluded','off']
    stages=[]
    for i,label in enumerate(labels):
        reach=label in ('reach-3','reach-5.2','moving-gate','combat-idle','combat-held','combined','occluded')
        box=label in ('box-2.22','box-0.4','combined','occluded')
        modules=dict(clicker=dict(enabled=label!='off'), reach=dict(enabled=reach, settings=dict(
            distance=3 if label=='reach-3' else 5.2, combatMode=label in ('combat-idle','combat-held'),
            onlyMoving=label=='moving-gate')), hitbox=dict(enabled=box,settings=dict(width=.4 if label=='box-0.4' else 2.22, deterministic=True)))
        stages.append(dict(at=i*5, modules=modules, mouse=[label=='combat-held',False], testClicksCps=0 if label=='off' else 18))
    return 60, stages

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('suite',choices=['aim','targeting'])
    parser.add_argument('--lead-seconds',type=int,default=45)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    duration,stages=globals()[args.suite]()
    epoch=int(time.time()*1000)+max(20,args.lead_seconds)*1000
    name=args.suite+'-settings-suite'
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(dict(name=name,duration=duration,startAt=epoch,stages=stages),indent=2),encoding='utf-8')
    command=dict(name=name,suite=args.suite,startAt=epoch,ticks=1800)
    print('/scriptevent aster:sequence '+json.dumps(command,separators=(',',':')))
    print('Run AsterBridgeTool --scenario',args.output,'<local-report.json> before that epoch, then enter the command in the disposable test world.')
