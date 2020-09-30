# Upgrading Fake DDK Roadmap
## Motivation
The Fake DDK has a few issues we are in the process of addressing:
- Inconsistent usage across packages
- Does not accurately conform to the workings of the driver_host
- Is missing features needed to test a number of drivers


Unfortunately, almost 200 packages depend on at least part of the fake_ddk,
 so in order to change it, we need to separate out dependencies.
  This roadmap describes what changes will be made at each stage, and how
 new packages can be written to avoid creating more transition work.

## Stage 1:  Create no_ddk    <--- CURRENT STAGE
#### Changes:
All packages which rely on fake_ddk simply for the C function definitions will be changed to depend on a new library, no_ddk.  In addition, many packages include fake_ddk.h unnecessarily, and this dependency will be removed.
#### New packages created during this stage:
New packages should not depend on fake_ddk if they do not instantiate any of the fake_ddk objects, or call any fake_ddk functions.  These packages should depend on no_ddk instead.

## Stage 2:  Separate non-Bind functionality
#### Changes:
All packages which rely on fake_ddk just for the fidl-helper functionality will be moved to a seperate library, name: tdb.  All usages of the fake_ddk which do not use the fake_ddk::Bind() function will be changed to depend on the new library.
#### New packages created during this stage:
New packages should not depend on fake_ddk if they do not call fake_ddk::Bind().  These packages should depend on no_ddk or the other fake library created in this stage instead.


## Stage 3:  Add functionality to fake_ddk::Bind class
#### Changes:
Functionality will be added to the fake_ddk::Bind class, which should cover cases where other testing setups derive their own Bind class.  Added functionality will be rolled into drivers that require this functionality.

#### New packages created during this stage:
- We ask that new testing rigs not create their own test rig that inherits from fake_ddk::Bind.  If you need additional testing functionality from fake_ddk, please contact @garratt.
- New packages should not depend on fake_ddk if they do not call fake_ddk::Bind().  These packages should depend on no_ddk or the other fake library created in this stage instead.

## Stage 4:  Fix additional bugs in fake_ddk
#### Changes:
Fake_ddk will be adapted to address some of its major issues, including lack of thread support, and launching multiple devices.

#### New packages created during this stage:
- We ask that new testing rigs not create their own test rig that inherits from fake_ddk::Bind.  If you need additional testing functionality from fake_ddk, please contact @garratt.
- New packages should not depend on fake_ddk if they do not call fake_ddk::Bind().  These packages should depend on no_ddk or the other fake library created in this stage instead.


## Stage 5:  Roll out fake_ddk testing to more drivers
#### Changes:
Fake DDK testing guidelines will be developed, and driver tests will be augmented to include fake_ddk tests, where appropriate.  Drivers that depended on no_ddk will now be moved back to depend on fake_ddk, and implement the recommended tests.

#### New packages created during this stage:
- Any new driver should now depend on fake_ddk and use the recommended testing procedure to test their drivers.
- We ask that new testing rigs not create their own test rig that inherits from fake_ddk::Bind.  If you need additional testing functionality from fake_ddk, please contact @garratt.
