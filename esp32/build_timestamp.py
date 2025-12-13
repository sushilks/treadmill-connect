import datetime

Import("env")

# Get current time
curr_time = datetime.datetime.now().strftime("%b %d %Y %H:%M:%S")

print(f"Injecting Build Timestamp: {curr_time}")

# Add the define. We must escape quotes.
env.Append(CPPDEFINES=[
    ("BUILD_TIMESTAMP", f'\\"{curr_time}\\"')
])
