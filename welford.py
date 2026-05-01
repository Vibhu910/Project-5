import math
import sys

class OnlineStats:
    def __init__(self):
        self.n = 0
        self.mean = 0.0
        self.M2 = 0.0

    def update(self, x):
        self.n += 1
        # Calculate delta with the old mean
        delta = x - self.mean
        # Update the running mean
        self.mean += delta / self.n
        # Calculate delta with the new mean
        delta2 = x - self.mean
        # Update the sum of squared differences
        self.M2 += delta * delta2

    @property
    def sample_variance(self):
        return self.M2 / (self.n - 1) if self.n > 1 else 0.0

    @property
    def stdev(self):
        return math.sqrt(self.sample_variance)

# Example: Processing a stream of integers
stats = {} #OnlineStats()
lastval = {}

args = ""
for line in sys.stdin:
    if "vlag" in line:
        try:
            value = int(line[5:])
        except Exception as e:
            continue
        if args not in stats:
            stats[args] = OnlineStats()
        stats[args].update(value)
    elif "slice" in line:
        continue
    else:
        args = line.replace('\n', '')

for args in stats:
    if stats[args].n > 5:
        print(f"{stats[args].mean:.2f} {args:s} --- stddev: {stats[args].stdev:.2f}")

