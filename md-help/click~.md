---
title: click~
description: impulse generator
categories:
 - object
pdcategory: cyclone, Signal Generators
arguments:
- type: float/list
  description: amplitude value(s) of the impulse
  default: 1
inlets:
  1st:
  - type: bang
    description: generates impulse
outlets:
  1st:
  - type: signal
    description: generated impulse

methods:
  - type: set <list>
    description: amplitude value(s) of the impulse (default 1)

---

[click~] generates an impulse when receiving a bang. An impulse is a single sample with a value of one followed by zeros, and this is the default impulse generated by [click~], but you can can also an impulse with a different amplitude value or even as a list of values (for band limited impulses - maximum is 256 sample values).
