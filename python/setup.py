from setuptools import find_packages, setup

EXCLUDE_FROM_PACKAGES = []

setup(name='bertini_real',
      version='1.6',  # TODO make this set programmatically
      description='Python library for bertini_real',
      url='http://bertinireal.com',
      author='Dan Hessler, Foong Min Wong, Danielle Amethyst Brake',
      author_email='brakeda@uwec.edu',
      packages=find_packages(exclude=EXCLUDE_FROM_PACKAGES),
      install_requires=['glumpy',
                        'matplotlib',
                        'trimesh',
                        'pyopengl',
                        'glfw',
                        'triangle'],
      package_dir={'bertini_real': 'bertini_real'},
      zip_safe=False)
