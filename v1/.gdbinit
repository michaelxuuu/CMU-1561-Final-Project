set startup-with-shell off
set detach-on-fork off
handle SIGUSR1 nostop
handle SIGUSR1 noprint

define lockthread
    set scheduler-locking step
end

