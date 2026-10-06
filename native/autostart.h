#ifndef HISTOGRAM_AUTOSTART_H
#define HISTOGRAM_AUTOSTART_H
#define BOOT_HOOK "/var/lib/webosbrew/init.d/60-histogramhyperion"
static const char boot_script[] = "#!/bin/sh\n# Histogram Hyperion own boot hook v1\n(\n  attempt=0\n  while [ -x /var/lib/webosbrew/init.d/60-histogramhyperion ] && [ \"$attempt\" -lt 120 ]; do\n    result=$(timeout 12 luna-send -n 1 luna://org.webosbrew.histogramhyperion.service/start '{}' 2>&1)\n    case \"$result\" in\n      *'\"running\":true'*) exit 0 ;;\n    esac\n    attempt=$((attempt + 1))\n    sleep 5\n  done\n  echo 'Histogram Hyperion boot start cancelled or timed out'\n) >/var/lib/webosbrew/histogramhyperion-boot.log 2>&1 </dev/null &\nexit 0\n";
static bool boot_enabled(void){return access(BOOT_HOOK,X_OK)==0;}
static bool set_boot(bool enabled){
 struct stat st;
 if(lstat(BOOT_HOOK,&st)==0){
  if(!S_ISREG(st.st_mode))return false;
  char existing[sizeof(boot_script)];int fd=open(BOOT_HOOK,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return false;
  ssize_t size=read(fd,existing,sizeof(existing));close(fd);
  if(size!=(ssize_t)(sizeof(boot_script)-1) || memcmp(existing,boot_script,(size_t)size)!=0)return false;
  if(!enabled)return unlink(BOOT_HOOK)==0;
  return chmod(BOOT_HOOK,0755)==0;
 }
 if(errno!=ENOENT)return false;
 if(!enabled)return true;
 if(mkdir("/var/lib/webosbrew",0755)!=0 && errno!=EEXIST)return false;
 if(mkdir("/var/lib/webosbrew/init.d",0755)!=0 && errno!=EEXIST)return false;
 int fd=open(BOOT_HOOK ".tmp",O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0700);if(fd<0)return false;
 size_t used=0;bool ok=true;
 while(used<sizeof(boot_script)-1){ssize_t n=write(fd,boot_script+used,sizeof(boot_script)-1-used);if(n<=0){ok=false;break;}used+=(size_t)n;}
 if(ok)ok=fchmod(fd,0755)==0 && fsync(fd)==0;
 if(close(fd)!=0)ok=false;
 if(ok)ok=rename(BOOT_HOOK ".tmp",BOOT_HOOK)==0;
 if(!ok)unlink(BOOT_HOOK ".tmp");
 return ok;
}
#endif
