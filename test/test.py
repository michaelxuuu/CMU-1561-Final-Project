import numpy as np

data = np.genfromtxt('out', delimiter=',', dtype=int)

# print("uniqueness", len(np.unique(data)) == len(data))

expected = set(range(0, 100000 * 10 + 1))
actual = set(data)

if expected == actual:
    print("passed")

