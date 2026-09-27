 # MODERN AUTOMATED PARKING SYSTEM

## 1. Slot monitoring algorithm

### Purpose
To enable drivers to see which slots are available before entering and the system should also update the display whenever a car enters or exists

### Algorithm

ALGORITHM SlotMonitor
INPUT: sensorArray[1..N]   // one ultrasonic/IR/camera sensor per bay
LOOP every T seconds (T = 1–2s):
    FOR each sensor i in sensorArray:
        state = READ(sensor[i])              // OCCUPIED / FREE
        IF state != SlotTable[i].status:
            SlotTable[i].status = state
            SlotTable[i].lastUpdated = NOW()
            PUSH_UPDATE(i, state) TO DisplayController   // e.g., MQTT publish
    freeCount = COUNT(SlotTable WHERE status = FREE)
    UPDATE EntranceDisplay WITH freeCount, freeSlotList
    IF freeCount == 0:
        SET EntranceBarrierMode = LOCKED_FULL
    ELSE:
        SET EntranceBarrierMode = NORMAL
END LOOP




## 2. Vehicle Entry Algorithm

### Purpose
The system should record information about every vehicle entering. It should also record the entry date and time.


### Algorithm

ALGORITHM VehicleEntry
TRIGGER: vehicle detected at entry loop / button pressed
1. IF freeCount == 0: DISPLAY "LOT FULL"; DENY entry; EXIT
2. CAPTURE plateImage VIA camera
3. plateNumber = ANPR_Recognise(plateImage)
      IF recognition confidence < threshold:
          plateNumber = "UNVERIFIED-" + ticketSerial   // fallback to ticket
4. entryID = GENERATE_UNIQUE_ID()
5. entryTime = NOW()
6. assignedSlot = FindNearestFreeSlot(SlotTable)
7. INSERT INTO Transactions (entryID, plateNumber, entryTime,
                             assignedSlot, status = 'ACTIVE')
8. SlotTable[assignedSlot].status = RESERVED
9. PRINT/ISSUE ticket (barcode = entryID) if plate unverifiable
10. CALL OpenBarrier(entryGate)
11. WAIT until vehicle clears loop sensor, THEN CloseBarrier(entryGate)



## 3. Barrier control Algorithm


### Algorithm

ALGORITHM BarrierControl(gateID, action)
IF action == OPEN:
    SET gate[gateID].signal = RAISE
    WAIT until gate[gateID].sensorState == FULLY_OPEN OR timeout(5s)
    LOG event(gateID, "OPENED", NOW())
ELSE IF action == CLOSE:
    IF InfraredBeam(gateID) == BLOCKED:     // safety check
        WAIT
    ELSE:
        SET gate[gateID].signal = LOWER
        LOG event(gateID, "CLOSED", NOW())



     
## 4. Time and fee calculation Algorithm

### Purpose
When a vehicle exits, the system should retrieve its entry time. 
The system should record the exit time. 
It should automatically calculate the total parking duration


### Algorithm

ALGORITHM CalculateFee(entryTime, exitTime, vehicleClass)
duration_minutes = (exitTime - entryTime) / 60
rateCard = LOOKUP RateTable WHERE class = vehicleClass
             AND isCurrent = TRUE

IF duration_minutes <= rateCard.gracePeriod:
    RETURN 0

billableHours = CEILING(duration_minutes / 60)   // round UP each started hour
IF billableHours <= rateCard.dailyCapHours:
    fee = billableHours * rateCard.hourlyRate
ELSE:
    fullDays = billableHours DIV 24
    remainderHours = billableHours MOD 24
    fee = fullDays * rateCard.dailyRate
        + MIN(remainderHours, rateCard.dailyCapHours) * rateCard.hourlyRate

RETURN fee   // in KES



## 5. Payment processing Algorithm

 
### Algorithm

ALGORITHM ProcessPayment(entryID, amountDue)
DISPLAY amountDue AND payment options [Cash, M-Pesa, Card]
choice = READ driver selection

SWITCH choice:
  CASE Cash:
      operatorConfirms = WAIT_FOR_OPERATOR_INPUT()
      IF operatorConfirms: status = PAID
  CASE M-Pesa:
      STK_PUSH(phoneNumber, amountDue)          // Daraja API
      response = POLL M-Pesa callback (timeout 60s)
      IF response.resultCode == 0: status = PAID
      ELSE: status = FAILED
  CASE Card:
      response = CardTerminal.charge(amountDue)
      IF response.approved: status = PAID
      ELSE: status = FAILED

IF status == PAID:
    UPDATE Transactions SET paidAmount = amountDue,
           paymentMethod = choice, paymentTime = NOW(), status = 'PAID'
    RETURN SUCCESS
ELSE:
    RETURN RETRY_OR_ESCALATE   // offer retry or manual operator override





## 6. Vehicle exit Algorithm


### Algorithm

ALGORITHM VehicleExit
TRIGGER: vehicle detected at exit loop
1. plateNumber/ticketID = ANPR_Recognise() OR SCAN(ticket barcode)
2. record = SELECT * FROM Transactions
             WHERE (plateNumber = plateNumber OR entryID = ticketID)
             AND status IN ('ACTIVE','PAID')
3. IF record NOT FOUND: ALERT operator (manual lookup); EXIT
4. exitTime = NOW()
5. fee = CalculateFee(record.entryTime, exitTime, record.vehicleClass)
6. IF record.status == 'PAID' AND record.paidAmount >= fee:
       CALL BarrierControl(exitGate, OPEN)
   ELSE:
       balanceDue = fee - record.paidAmount (if any)
       CALL ProcessPayment(record.entryID, balanceDue)
       IF payment SUCCESS:
           CALL BarrierControl(exitGate, OPEN)
       ELSE:
           HOLD vehicle; notify operator
7. UPDATE Transactions SET exitTime = exitTime, status = 'CLOSED'
8. SlotTable[record.assignedSlot].status = FREE
9. PUSH_UPDATE to Display Module (M1 freeCount++)



## DATA STRUCTURES 

# Arrays
An array or list can store available parking slots.
Example:
AvailableSlots = [A01, A03, A07, A12, A18]
Reasons for use
	Easy to store multiple parking slots. 
	Easy to search through available slots. 
	Makes it easy to display available spaces. 
	Can be updated when vehicles enter or leave. 

# Record/structure
A vehicle can be represented using a structure/record.
Example:
Vehicle
-------------------------
Vehicle_ID
Registration_Number
Vehicle_Type
Owner_Name
Reasons for use
   It groups related information about a vehicle into one logical unit.


# Queue
A queue can be used when there are more vehicles waiting to enter than available spaces.
It follows:
FIFO — First In, First Out
When a parking slot becomes available, the vehicle at the front can be considered first.
Reasons for use
	Maintains fairness. 
	Handles congestion at the entrance. 
	Suitable when the parking facility is full. 


# Stack
It could be used for certain temporary operations such as maintaining recent system actions.
It follows:
LIFO — Last In, First Out

# Hash tables
A hash table can provide fast lookup of vehicles using their registration number.
Example:
"KDA123A" → Parking Session 105
"KBB456B" → Parking Session 106
"KDC789C" → Parking Session 107
Reasons for use
   When a vehicle arrives at the exit, the system can quickly locate its active parking session.


# Boolean values
Boolean values can represent parking slot availability.
A01 → TRUE
A02 → FALSE
A03 → TRUE
Where:
	TRUE = available 
	FALSE = occupied 
Reasons for use
     It provides a simple way of representing whether a slot is available.
     
     

 
