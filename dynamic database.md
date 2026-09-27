# Automated Parking Management System 🅿️

A modular system design for an automated parking management system built for the Kenyan
market — real-time slot sensing, ANPR-based vehicle entry, automatic fee calculation, and
M-Pesa/cash/card payment processing with an offline-resilient billing engine.

> This repository documents the **system design**: requirements analysis, module
> breakdown, algorithms, data structures, and a dynamic relational database schema.
> Implementation code (C++ core logic / backend) lives alongside this design as the
> project develops.

---

## Table of Contents

- [Overview](#overview)
- [Requirements Analysis](#requirements-analysis)
- [System Modules](#system-modules)
- [Data Structures](#data-structures)
- [Database Design](#database-design)
- [Module Interaction](#module-interaction)
- [Tech Notes](#tech-notes)
- [Roadmap](#roadmap)

---

## Overview

The client's brief asks for a parking system that:

1. Lets drivers **see available slots** before entering
2. **Records vehicles** on arrival
3. **Calculates time spent and fee** on exit
4. **Opens the barrier** once payment is confirmed

Beyond the literal brief, a real Kenyan deployment also has to handle **connectivity
resilience** (M-Pesa/network can drop) and **auditability** (management needs occupancy
and revenue reports) — both of which shape the design below.

## Requirements Analysis

| # | Stated requirement | Implicit requirements it hides |
|---|---|---|
| 1 | Drivers must **see** available slots before entry | Real-time slot occupancy sensing; an entrance display; continuous push updates |
| 2 | System **records vehicles** on arrival | Unique vehicle ID (ANPR or ticket), timestamping, entry barrier control, "lot full" handling |
| 3 | System **calculates time & fee** on exit | Billing/rate engine, entry-record matching, lost-ticket handling, tiered rates, KES currency |
| 4 | **Barrier opens** on payment | Payment gateway (cash / M-Pesa / card), payment–actuator confirmation loop, receipt |

Cross-cutting, non-negotiable concerns:

- **Connectivity resilience** — offline queueing so mobile-money/network outages don't block exits or lose revenue.
- **Auditability** — a persistent transactional database for occupancy trends, revenue, and peak-hour reporting.

## System Modules

Eight modules across three layers:

| Layer | Modules |
|---|---|
| Sensing / Display | **M1** Slot Sensing & Availability Display |
| Transaction Processing | **M2** Vehicle Entry & Registration · **M3** Barrier & Actuator Control · **M4** Time & Fee Calculation · **M5** Payment Processing · **M6** Vehicle Exit |
| Management / Reporting | **M7** Database / Persistence · **M8** Admin, Reporting & Rate Configuration |

Each module is independently developed, tested, and scaled — e.g. ANPR cameras can be
upgraded without touching the fee engine — with **M7** as the single source of truth
binding sensing, transactions, and reporting together.

Key algorithmic behaviour per module:

- **M1** — event-driven sensor polling (1–2s loop), pushes slot state to the display and locks the entrance barrier when full.
- **M2** — denies entry when full, runs ANPR with a ticket-serial fallback below a confidence threshold, assigns the nearest free slot via a min-heap.
- **M3** — shared entry/exit barrier control with an infrared safety interlock before closing.
- **M4** — grace period, hourly billing rounded up, daily-rate cap for long stays.
- **M5** — Cash / M-Pesa (Daraja STK push) / Card, with an **offline queue** (`status = PENDING_SYNC`) so a dropped connection never blocks the barrier.
- **M6** — matches the exiting vehicle to its open transaction, settles any balance due, then frees the slot and pushes the update back to M1.

## Data Structures

| Structure | Used in | Why chosen |
|---|---|---|
| 2D Array / Matrix (SlotGrid) | M1 | O(1) direct-index access per bay; mirrors the physical layout |
| Hash Table (plate → active transaction) | M2, M6 | O(1) average lookup at exit — critical to avoid queues at the gate |
| Min-Heap / Priority Queue (free slots by distance) | M2 | O(log n) retrieval of the *nearest* free slot |
| Queue (FIFO) | Entry lane sequencing, offline-payment sync (M5) | Fair, in-order processing; nothing pending is skipped |
| Stack (LIFO) | Operator "undo last action" | Matches undo-most-recent behaviour |
| Linked List | Occupied-slot history per bay (M7) | Variable-length history without fixed pre-allocation |
| B-Tree Index (plateNumber, entryTime) | Database engine (M7) | Keeps lookups/range queries fast as the transaction table grows into the millions |
| Struct/Record | Transaction, Vehicle, RateCard — all modules | Groups related fields as one logical unit through create → update → close |

## Database Design

The schema is **dynamic**: it supports live status updates (slot occupancy, active
transactions) *and* permanent historical records, with rate/fee rules management can
change without touching code.

### Entity–Relationship Overview

```
Vehicle (1) ---< (M) Transaction (M) >--- (1) Slot
Transaction (1) ---< (M) Payment
RateCard (1) ---< (M) Transaction
Gate (1) ---< (M) GateLog
Operator (1) ---< (M) Transaction   [who handled/overrode]
```

### Tables

<details>
<summary><strong>Slot</strong> — real-time occupancy state</summary>

| Column | Type | Notes |
|---|---|---|
| slot_id | INT PK | |
| zone | VARCHAR(10) | e.g. "A", "B" for multi-floor lots |
| distance_from_entrance | INT | used by the nearest-slot algorithm |
| status | ENUM('FREE','OCCUPIED','RESERVED','OUT_OF_SERVICE') | updated live by M1 |
| last_updated | DATETIME | |
</details>

<details>
<summary><strong>Vehicle</strong></summary>

| Column | Type | Notes |
|---|---|---|
| plate_number | VARCHAR(15) PK | |
| vehicle_class | ENUM('SALOON','SUV','MOTORCYCLE','TRUCK') | drives rate lookup |
| first_seen | DATETIME | |
</details>

<details>
<summary><strong>Transaction</strong> — the dynamic core table</summary>

| Column | Type | Notes |
|---|---|---|
| entry_id | BIGINT PK | |
| plate_number | VARCHAR(15) FK → Vehicle | nullable if ANPR failed |
| ticket_barcode | VARCHAR(30) | fallback identifier |
| slot_id | INT FK → Slot | |
| entry_time | DATETIME | |
| exit_time | DATETIME NULL | NULL while parked |
| rate_id | INT FK → RateCard | rate in force at entry time |
| fee_due | DECIMAL(10,2) | computed at exit |
| paid_amount | DECIMAL(10,2) DEFAULT 0 | |
| status | ENUM('ACTIVE','PENDING_SYNC','PAID','CLOSED') | |
</details>

<details>
<summary><strong>Payment</strong></summary>

| Column | Type | Notes |
|---|---|---|
| payment_id | BIGINT PK | |
| entry_id | BIGINT FK → Transaction | |
| method | ENUM('CASH','MPESA','CARD') | |
| amount | DECIMAL(10,2) | |
| mpesa_receipt | VARCHAR(20) NULL | for M-Pesa reconciliation |
| paid_at | DATETIME | |
</details>

<details>
<summary><strong>RateCard</strong> — management-editable (the "dynamic" part)</summary>

| Column | Type | Notes |
|---|---|---|
| rate_id | INT PK | |
| vehicle_class | ENUM | |
| hourly_rate | DECIMAL(8,2) | KES |
| daily_rate | DECIMAL(8,2) | |
| grace_period_minutes | INT | free minutes before billing starts |
| daily_cap_hours | INT | |
| effective_from | DATETIME | preserves history through rate changes |
| effective_to | DATETIME NULL | NULL = currently active |
</details>

<details>
<summary><strong>Gate / GateLog</strong></summary>

| Column | Type | Notes |
|---|---|---|
| gate_id | INT PK | |
| gate_type | ENUM('ENTRY','EXIT') | |
| log_id | BIGINT PK (GateLog) | |
| gate_id | FK | |
| event | ENUM('OPENED','CLOSED','FAULT') | |
| event_time | DATETIME | |
</details>

<details>
<summary><strong>Operator</strong></summary>

| Column | Type | Notes |
|---|---|---|
| operator_id | INT PK | |
| name | VARCHAR(50) | |
| role | ENUM('ATTENDANT','SUPERVISOR','ADMIN') | |
</details>

### Why it's "dynamic"

- **Slot** is continuously overwritten (`UPDATE`, not `INSERT`) to reflect real-time occupancy — read constantly by M1 for the display, but kept lightweight.
- **Transaction** grows append-mostly and doubles as both the live record for a parked car and the closed historical record — no separate "current occupancy" table, so nothing falls out of sync.
- **RateCard**'s `effective_from` / `effective_to` versioning lets fees change without corrupting the calculation for past, already-billed transactions.
- Foreign keys tying **Payment** and **GateLog** back to `entry_id` let the full lifecycle of any vehicle visit be reconstructed for dispute resolution.

## Module Interaction

```
 [M1 Slot Sensors] --status--> [Display Board]
        |
        v
 [M2 Entry] --record--> [M7 Database] <--record-- [M6 Exit]
        |                                    ^
        v                                    |
 [M3 Barrier: Entry]                 [M4 Fee Calc] --> [M5 Payment] --> [M3 Barrier: Exit]
                                                                |
                                                                v
                                                        [M8 Admin/Reports]
```

## Tech Notes

- **Currency**: all fees are computed and stored in KES.
- **Payments**: M-Pesa integration via the Daraja API (STK push); cash and card fall back to operator confirmation.
- **Resilience**: any payment or transaction that can't reach the central DB is queued locally as `PENDING_SYNC` and synced once connectivity returns.
- **In-memory structures** (hash table, min-heap, queue, stack) back the live runtime state; the relational schema above is the persistent source of truth.

## Roadmap

- [ ] Core C++ simulation of M1–M6 (in-memory data structures)
- [ ] Database schema migration scripts
- [ ] M-Pesa Daraja API integration
- [ ] Admin/reporting dashboard (M8)
- [ ] Hardware integration (ANPR camera, barrier actuator, sensors)

---

*System design report for an Automated Parking Management System — Kenya deployment.*
