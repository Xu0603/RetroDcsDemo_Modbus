def on_start(ctx):
    ctx.log("INFO", "weighted calculation started")


def on_timer(ctx):
    values = ctx.read_many(["temperature", "pressure"])
    temperature = values["temperature"].value
    pressure = values["pressure"].value
    result = temperature * 0.6 + pressure * 0.4
    ctx.write("calculated_value", result)
    print(f"temperature={temperature}, pressure={pressure}, result={result:.6f}")
    ctx.log("INFO", f"calculated_value={result:.6f}")


def on_stop(ctx):
    ctx.log("INFO", "weighted calculation stopped")
