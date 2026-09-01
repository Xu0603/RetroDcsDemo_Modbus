"""Created by the configuration workstation."""

def on_start(ctx):
    ctx.log("INFO", "script started")

def on_timer(ctx):
    # value = ctx.read("alias").value
    pass

def on_stop(ctx):
    ctx.log("INFO", "script stopped")
