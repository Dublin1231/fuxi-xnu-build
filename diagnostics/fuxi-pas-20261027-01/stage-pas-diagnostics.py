"""Verify the pristine locked source; --stage adds only the diagnostic patch."""
from pathlib import Path
import argparse,hashlib,json,subprocess
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('source',type=Path);ap.add_argument('--stage',action='store_true');a=ap.parse_args();src=a.source.resolve();here=Path(__file__).resolve().parent;l=json.loads((here/'source-lock.json').read_text())
 def git(*args):return subprocess.run(['git','-C',str(src),*args],check=True,capture_output=True,text=True).stdout.strip()
 if git('rev-parse','HEAD')!=l['commit'] or git('status','--porcelain'):raise SystemExit('Requires pristine exact pinned checkout')
 if sha(src/'xnu/fuxi-adreno/fuxi_adreno.c')!=l['baseline_driver_sha256']:raise SystemExit('Driver source drift')
 for row in l['kernel_patches']:
  if sha(src/row['file'])!=row['sha256']:raise SystemExit('Kernel patch drift '+row['file'])
 patch=here/'0111-fuxi-pas-full-width-args.patch';dest=src/'xnu/patches'/patch.name
 if sha(patch)!=l['patch_sha256'] or dest.exists():raise SystemExit('Patch drift or already staged')
 if a.stage:
  with dest.open('xb') as f:f.write(patch.read_bytes())
 print(json.dumps({'source_commit':l['commit'],'diagnostic_patch_staged':a.stage,'build_executed':False,'phone_commands':0}))
if __name__=='__main__':main()
