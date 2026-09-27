Dynamic Database Design
Automated Parking Management System

“Entity–Relationship Overview
Vehicle (1) ---< (M) Transaction (M) >--- (1) Slot Transaction (1) ---< (M) Payment
RateCard (1) ---< (M) Transaction
Gate (1) ---< (M) GateLog
Operator (1) ---< (M) Transaction   [who handled / overrode]
1.	Table Definitions
Slot — real-time occupancy state
Column	Type	Notes
slot_id	INT PK	
zone	VARCHAR(10)	e.g. "A", "B" for multi-floor lots
distance_from_ent rance	INT	metres/units, used by nearest-slot algorithm
status	ENUM('FREE','OCC
UPIED', 'RESERVE
D','OUT_OF_SERVI
CE')	updated in real time by M1
last_updated	DATETIME	
Vehicle
Column	Type	Notes
plate_number	VARCHAR(15) PK	
vehicle_class	ENUM('SALOON','S
UV', 'MOTORCYCL
E','TRUCK')	drives rate lookup
first_seen	DATETIME	
Transaction — the dynamic core table
Column	Type	Notes
entry_id	BIGINT PK	
plate_number	VARCHAR(15) FK
-> Vehicle	nullable if ANPR failed (ticket-only)
ticket_barcode	VARCHAR(30)	fallback identifier
slot_id	INT FK -> Slot	
entry_time	DATETIME	
exit_time	DATETIME NULL	NULL while vehicle is parked
rate_id	INT FK -> RateCard	rate in force at entry time
fee_due	DECIMAL(10,2)	computed at exit
paid_amount	DECIMAL(10,2)
DEFAULT 0	
status	ENUM('ACTIVE','PE
NDING_SYNC',
'PAID','CLOSED')	
Payment
Column	Type	Notes
payment_id	BIGINT PK	
entry_id	BIGINT FK ->
Transaction	
method	ENUM('CASH','MPE
SA','CARD')	
amount	DECIMAL(10,2)	
mpesa_receipt	VARCHAR(20)
NULL	for M-Pesa reconciliation
paid_at	DATETIME	
RateCard — management-editable (makes the schema “dynamic”)
Column	Type	Notes
rate_id	INT PK	
vehicle_class	ENUM	
hourly_rate	DECIMAL(8,2)	in KES
daily_rate	DECIMAL(8,2)	
grace_period_min utes	INT	free minutes before billing starts
daily_cap_hours	INT	
effective_from	DATETIME	allows rate changes without deleting history
effective_to	DATETIME NULL	NULL = currently active
Gate / GateLog
Column	Type	Notes
gate_id	INT PK	
gate_type	ENUM('ENTRY','EXI
T')	
log_id	BIGINT PK
(GateLog)	
gate_id	FK	
event	ENUM('OPENED','C
LOSED','FAULT')	
event_time	DATETIME	
Operator
Column	Type	Notes
operator_id	INT PK	
name	VARCHAR(50)	
role	ENUM('ATTENDAN
T','SUPERVISOR','A
DMIN')	
 
