.section my_heap,heap
;.space 0x0500

; To let cJSON lib work well increase heap size
.space 0x0AFF


;test heap memory reduced for deallocating memory in constructors methods
;.space 0x001F
 