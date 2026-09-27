

## Database Design



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

