import numpy as np
z=np.random.random((10000,10000))
z1=np.random.random((10000,10000))
z2=np.random.random((10000,10000))
for x in range(1000):
    z3=np.random.random((10000,10000))
    z4=np.random.random((10000,10000))
    z1+=z3*z4
print(z.sum())
