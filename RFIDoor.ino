	/**
	 * General config
	 * 	Here you can enable or disable features
	 */
	
		#define ENABLE_RFID
		#define ENABLE_KEYPAD

		// Comment out to enable
		// #define DEBUG_ENABLED

	/**
	 * Some macros
	 * 	This macros will help us to "delete" code if a feature is disabled
	 * 		Usage: _MACRO(code)
	 * 	Although, in some cases this macro will not work, then we need to use #ifdef
	 */
		#ifdef ENABLE_RFID
			#define _RFID(code)	code
		#else
			#define _RFID(code)
		#endif

		#ifdef ENABLE_KEYPAD
			#define _KEYPAD(code)	code
		#else
			#define _KEYPAD(code)
		#endif

	// Including the required librares

	#include <DebugUtils.h>

	#include <KeyStore.h>

	#include <SPI.h>
	#include <MFRC522.h>

	#include <OneWireKeys.h>

	/**
	 * Pinout
	 */

		#define PCD_SS		4

		#define KEYPAD_PIN	A3			// #3

		#define CONFIG_PIN	SCK			// A1 - #2
		#define BUZZER_PIN	KEYPAD_PIN	// A3 - #3
		#define RELAY_PIN	5

	/**
	 * UI and other things
	 */
	
		/**
		 * Reset procedure
		 */
			
			#define RESETP_TIME	20000

			#define RESETP_LOW	20
			#define RESETP_HIGH	1003

		/**
		 * RFID
		 */
		
			#define PCD_GAIN MFRC522::RxGain_max // RxGain_48dB

		/**
		 * Keypad
		 */
			#define KEYPAD_SIZE	16

			#ifdef ENABLE_KEYPAD
				char keypad_keys[KEYPAD_SIZE] =
				{
					'1', '2', '3', 'A',
					'4', '5', '6', 'B',
					'7', '8', '9', 'C',
					'*', '0', '#', 'D'
				};

				uint8_t keypad_values[KEYPAD_SIZE] =
				{
					180, 139, 133, 126,
					170, 115, 104, 93,
					157, 77, 63, 44,
					146, 40, 18, 0
				};
			#endif

			#define KEYPAD_SAMPLES		3
			#define KEYPAD_TOLERANCE 	10
			#define KEYPAD_PRESS_TIME	20

		/**
		 * (Beep) times :)
		 */

			#define DELAY_AFTER_MODKEY		500

			#define BUZZER_TIME_READYFORKEY		50
			#define BUZZER_TIMES_READYFORKEY	2

			#define BUZZER_TIME_OK				50
			#define BUZZER_TIMES_OK				1

			#define BUZZER_TIME_ERROR			500
			#define BUZZER_TIMES_ERROR			3

			#define BUZZER_TIME_WRONGKEY		500
			#define BUZZER_TIMES_WRONGKEY		1

		/**
		 * Relay
		 */

			#define RELAY_TIME_MIN	0		// None
			#define RELAY_TIME_MAX	10000	// 10 seconds

	/**
	 * KeyStore will be using: 
	 * 		KEYS * (KEY_SIZE + sizeof(KeyData))
	 *
	 *    = 28 * (10 + 1)
	 *    = 308 EEPROM bytes
	 *    
	 *    = 28  * (6 + 1)
	 *    = 196 EEPROM bytes
	 *
	 * 	308 + 196 = 504 bytes (fits into ATtiny85)
	 */

		#define RFID_KEYS		28
		#define RFID_KEY_SIZE	10

		#define KEYCODE_KEYS		28
		#define KEYCODE_KEY_SIZE	6

	
	// Finally, the code :)


	/**
	 * SPI related functions
	 */
	
		#ifdef ENABLE_RFID
			bool spi_pcd_ss = HIGH;
			bool spi_available = false;

			void disableSPI()
			{
				// Keep the original value of PCD_SS stored
				spi_pcd_ss = digitalRead(PCD_SS);

				// Disable the bus and the reader
				digitalWrite(PCD_SS, HIGH);
				SPI.end();

				// Keep track of the bus status
				spi_available = false;
			}

			void enableSPI()
			{
				// Enable the bus and restore PCD_SS original value
				SPI.begin();
				digitalWrite(PCD_SS, spi_pcd_ss);

				// As specified in https://www.arduino.cc/en/Reference/SPIBegin, 
				// SPI.begin() does not configure MISO as input
				pinMode(MISO, INPUT);

				// Keep track of the bus status
				spi_available = true;
			}
		#else
			void disableSPI() {}
			void enableSPI() {}
		#endif

	/**
	 * Config related functions
	 * 	CONFIG_PIN will be connected to SPI SCK through a 10K resistor. 
	 */
	
		uint16_t readConfig()
		{
			// Disable the bus so we can use the pins
			disableSPI();

			// Is pinMode INPUT needed?
			uint16_t value = analogRead(CONFIG_PIN);

			// Re enable the bus
			enableSPI();

			return value;
		}

	/**
	 * Buzzer related functions
	 * 	The buzzer will be connected to the keypad pin through a pair of PNP and NPN transistors. 
	 * 	When the user presses a key, the pin goes LOW, and the transistors get saturated. 
	 * 	If we want to drive the buzzer, we can ONLY drive the input LOW
	 * 	If we drive it HIGH, a short circuit can occur if the user presses the some non-resistor-divided key
	 * 	Also, when the user presses a keypad key, the buzzer gets enabled. 
	 */
	
		void beep(uint16_t time, uint8_t times = 1)
		{
			// Beep :)
			for(; times > 0; times--)
			{
				pinMode(BUZZER_PIN, OUTPUT);
				digitalWrite(BUZZER_PIN, LOW);
				delay(time);

				pinMode(BUZZER_PIN, INPUT);
				delay(time);
			}
		}

	/**
	 * Relay related functions
	 */
	
		uint32_t relay_enabled_at = 0;

		void enableRelay()
		{
			// Enable the relay
			digitalWrite(RELAY_PIN, HIGH);

			// Keep track of the time :)
			relay_enabled_at = millis();
		}

		void disableRelay()
		{
			// Disable the relay
			digitalWrite(RELAY_PIN, LOW);

			// Keep (not anymore) track of the time
			relay_enabled_at = 0;
		}

		void updateRelay()
		{
			if(digitalRead(RELAY_PIN))
				if((millis() - relay_enabled_at) >= map(readConfig(), 0, 1023, RELAY_TIME_MIN, RELAY_TIME_MAX))
					disableRelay();
		}

	/**
	 * RFID reader
	 */
	
		_RFID
		(
			MFRC522 RFID(PCD_SS, MFRC522::UNUSED_PIN);
			uint8_t UID[RFID_KEY_SIZE];

			bool readPICC()
			{
				// If SPI pins are not being used by any other function
				if(spi_available)
				{
					// If a new PICC is placed near the reader
					if(!RFID.PICC_IsNewCardPresent())
						return false;

					// Read the PICC
					if(!RFID.PICC_ReadCardSerial())
						return false;

					// Put the valid bytes into UID
					for(int i = 0; i < RFID.uid.size; i++)
						UID[i] = RFID.uid.uidByte[i];

					// Zero-fill the rest
					for(int i = RFID.uid.size; i < RFID_KEY_SIZE; i++)
						UID[i] = 0x00;

					// Stop reading
					RFID.PICC_HaltA();

					return true;
				}

				return false;
			}
		)

	/**
	 * Keypad
	 */
	
		#ifdef ENABLE_KEYPAD
			OneWireKeys<KEYPAD_PIN, KEYPAD_SIZE, KEYPAD_SAMPLES, KEYPAD_TOLERANCE> Keypad(keypad_keys, keypad_values, KEYPAD_PRESS_TIME);
			uint8_t KeyCode[KEYCODE_KEY_SIZE];

			uint8_t getCurrentKeyCodeLength()
			{
				uint8_t length = 0;

				for(; length < KEYCODE_KEY_SIZE; length++)
					if(KeyCode[length] == 0)
						break;

				return length;
			}

			bool readKeyCode()
			{
				uint8_t keycode_length = getCurrentKeyCodeLength();

				// If there are still some characters to add
				if(keycode_length < KEYCODE_KEY_SIZE)
				{
					// Get the char
					char keycode_char = Keypad.readKey();

					// If is a valid character
					if(keycode_char != NO_KEY)
					{
						DEBUG(uint8_t keycode_chars[1] = {keycode_char});
						DEBUG_SERIAL_PRINTA("KeyPad key pressed: 0x", keycode_chars, 1, HEX);

						// Add it to Key
						KeyCode[keycode_length] = keycode_char;

						// AND, if there was only 1 character left to add
						if(keycode_length == (KEYCODE_KEY_SIZE - 1))
							return true;
					}

					return false;
				}

				// If there's no characters to add the key is valid, which means true was already returned
				// We need to clear up KeyCode
				for(uint8_t i = 0; i < KEYCODE_KEY_SIZE; i++)
					KeyCode[i] = 0;

				return false;
			}
		#endif

	/**
	 * Miscelaneous functions
	 */
	
		bool keyIsEqual(uint8_t* key1, uint8_t* key2, uint8_t size)
		{
			do
			{
				size--;

				if(key1[size] != key2[size])
					return false;
			}
			while(size != 0);

			return true;
		}

	/**
	 * KeyStore
	 */
	
		struct KeyData
		{ bool master = false; };

		KeyStore<RFID_KEYS, RFID_KEY_SIZE, 0, KeyData> RFIDKeyStore;
		KeyStore<KEYCODE_KEYS, KEYCODE_KEY_SIZE, KEYSTORE_EEPROM_NEXT_ADDRESS(RFID_KEYS, RFID_KEY_SIZE, 0, KeyData), KeyData> KeyCodeKeyStore;

	/**
	 * User interface
	 */
	
		/**
		 * Reset procedure: 
		 * 	1. Power-off the device. 
		 * 	2. Take CONFIG to RESETP_LOW. 
		 * 	3. Power-on the device. 
		 * 	4. In the following RESETP_TIME ms (default is 20s), take CONFIG to RESETP_HIGH and then to RESETP_LOW again. 
		 * 	5. If you enabled RFID, place the MASTER TAG near the RFID reader. 
		 * 	6. If you enabled KEYPAD, Enter the MASTER CODE into the keypad. 
		 */

		void resetProcedure()
		{
			// Check the reset vector ? (only if pin #5 is not used)

			// Take CONFIG_PIN to RESETP_LOW
			if(readConfig() >= RESETP_LOW)
				return;

			DEBUG_SERIAL_PRINT("Reset procedure: ");
			DEBUG_SERIAL_PRINT("\tCONFIG_PIN => LOW");

			beep(BUZZER_TIME_OK, BUZZER_TIMES_OK);

			// In the following RESETP_TIME ms
			while(millis() < RESETP_TIME)
			{
				// Take CONFIG_PIN to HIGH
				if(readConfig() <= RESETP_HIGH)
					continue;

				DEBUG_SERIAL_PRINT("\tCONFIG_PIN => HIGH");

				beep(BUZZER_TIME_OK, BUZZER_TIMES_OK);

				// In the following RESETP_TIME ms
				while(millis() < RESETP_TIME)
				{
					// Take CONFIG_PIN to RESETP_LOW (again)
					if(readConfig() >= RESETP_LOW)
						continue;

					DEBUG_SERIAL_PRINT("\tCONFIG_PIN => LOW");

					beep(BUZZER_TIME_OK, BUZZER_TIMES_OK);

					KeyData master_key_data;
					master_key_data.master = true;

					// RFID
					_RFID
					(
						// Clear the KeyStore
						DEBUG_SERIAL_PRINT("\tClearing RFID KeyStore...");
						RFIDKeyStore.clear();
						DEBUG_SERIAL_PRINT("\t\tok.");

						// Beep :)
						beep(BUZZER_TIME_READYFORKEY, BUZZER_TIMES_READYFORKEY);

						// Wait for the PICC
						DEBUG_SERIAL_PRINT("\tWaiting for MASTER tag");
						while(!readPICC())
							;

						delay(DELAY_AFTER_MODKEY);

						// Add the key as master
						DEBUG_SERIAL_PRINT("\tAdding MASTER tag to RFID KeyStore...");
						RFIDKeyStore.addKey(UID, &master_key_data);
						DEBUG_SERIAL_PRINT("\t\tok.");

						// Beep :)
						beep(BUZZER_TIME_OK, BUZZER_TIMES_OK);
					)

					// KeyCode
					_KEYPAD
					(
						// Clear the KeyStore
						DEBUG_SERIAL_PRINT("\tClearing KeyCode KeyStore");
						KeyCodeKeyStore.clear();
						DEBUG_SERIAL_PRINT("\t\tok.");

						// Beep :)
						beep(BUZZER_TIME_READYFORKEY, BUZZER_TIMES_READYFORKEY);

						// Wait for the KeyCode
						DEBUG_SERIAL_PRINT("\tWaiting for MASTER KeyCode");
						while(!readKeyCode())
							;

						delay(DELAY_AFTER_MODKEY);

						// Add the key as master
						DEBUG_SERIAL_PRINT("\tAdding MASTER KeyCode to KeyCode KeyStore...");
						KeyCodeKeyStore.addKey(KeyCode, &master_key_data);
						DEBUG_SERIAL_PRINT("\t\tok.");

						// Beep :)
						beep(BUZZER_TIME_OK, BUZZER_TIMES_OK);
					)

					DEBUG_SERIAL_PRINT("Reset procedure finished");
					return;
				}
			}

			DEBUG_SERIAL_PRINT("Exiting from reset procedure");
		}


	void setup()
	{
		// !DEBUG Init the Serial port
		// Remember that in the ATtiny boards you don't have a Serial port (depending on which core you're using). 
		// BUT, you can develop the project on a UNO (ATmega328) for sure (as I did :P)
		DEBUG_SERIAL_BEGIN();

		// Set RELAY_PIN as OUTPUT
		pinMode(RELAY_PIN, OUTPUT);

		_RFID
		(
			// Init the SPI bus
			enableSPI();

			// Init the PCD
			RFID.PCD_Init();
			RFID.PCD_SetAntennaGain(PCD_GAIN);

			DEBUG_SERIAL_PRINT("Init ready");
		)

		// Check if the user wants to reset the data
		resetProcedure();

		beep(BUZZER_TIME_OK, BUZZER_TIMES_OK);
	}

	KeyData key_data;

	void loop()
	{
		// Update relay's status
		updateRelay();

		_RFID
		(
			// If a PICC is placed near the reader
			if(readPICC())
			{
				DEBUG_SERIAL_PRINT("A PICC was detected: ");
				DEBUG_SERIAL_PRINTA("\tUID: 0x", UID, RFID_KEY_SIZE, HEX);

				// If the tag is valid
				if(RFIDKeyStore.keyIsValid(UID, &key_data))
				{
					DEBUG_SERIAL_PRINT("\tThe PICC is valid");

					// If is a MASTER tag
					if(key_data.master)
					{
						DEBUG_SERIAL_PRINT("\tThe PICC is MASTER");

						// Beep :)
						beep(BUZZER_TIME_READYFORKEY, BUZZER_TIMES_READYFORKEY);

						/**
						 * Wait for the PICC
						 * 	Only continue if the current PICC is different from the previous MASTER PICC. 
						 * 	Then, a MASTER tag can't delete itself, and we can prevent situations in which
						 * 	there's no MASTER tags in the KeyStore.
						 */
						
						// Copy MASTER UID into master_uid
						uint8_t master_uid[RFID_KEY_SIZE];
						for(uint8_t i = 0; i < RFID_KEY_SIZE; i++)
							master_uid[i] = UID[i];

						// Wait until a non-master tag is read
						DEBUG_SERIAL_PRINT("\tWaiting for a different tag");
						while(!readPICC() || keyIsEqual(master_uid, UID, RFID_KEY_SIZE))
							;

						delay(DELAY_AFTER_MODKEY);

						// If the key is not stored in the memory
						if(!RFIDKeyStore.keyIsValid(UID))
						{
							DEBUG_SERIAL_PRINT("\tThe PICC is NOT valid, adding...");
							// Add the key
							key_data.master = false;

							// If the key was successfully added
							if(RFIDKeyStore.addKey(UID, &key_data))
							{
								DEBUG_SERIAL_PRINT("\t\tok.");

								beep(BUZZER_TIME_OK, BUZZER_TIMES_OK);
							}
							else
							{
								DEBUG_SERIAL_PRINT("\t\terror, the KeyStore may be full.");

								beep(BUZZER_TIME_ERROR, BUZZER_TIMES_ERROR);
							}
						}
						// If IS stored => Remove the key
						else
						{
							DEBUG_SERIAL_PRINT("\tThe PICC is valid, removing...");

							RFIDKeyStore.removeKey(UID);

							beep(BUZZER_TIME_WRONGKEY, BUZZER_TIMES_WRONGKEY);

							DEBUG_SERIAL_PRINT("\t\tok.");
						}
					}
					// If is not a MASTER tag
					else
					{
						DEBUG_SERIAL_PRINT("\t\tRelay enabled");

						// Enable the relay :P
						enableRelay();

						// Beep :)
						// Shall we?
						beep(BUZZER_TIME_OK, BUZZER_TIMES_OK);
					}
				}
				else
				{
					DEBUG_SERIAL_PRINT("\tThe PICC is NOT valid");

					beep(BUZZER_TIME_WRONGKEY, BUZZER_TIMES_WRONGKEY);
				}
			}
		)

		_KEYPAD
		(
			// If a key is successfully read
			if(readKeyCode())
			{
				DEBUG_SERIAL_PRINT("A KeyCode was introduced: ");
				DEBUG_SERIAL_PRINTA("\tKeyCode: 0x", KeyCode, KEYCODE_KEY_SIZE, HEX);

				// If the code is valid
				if(KeyCodeKeyStore.keyIsValid(KeyCode, &key_data))
				{
					DEBUG_SERIAL_PRINT("\tThe KeyCode is valid");

					// If is a MASTER code
					if(key_data.master)
					{
						DEBUG_SERIAL_PRINT("\tThe KeyCode is MASTER");

						// Beep :)
						beep(BUZZER_TIME_READYFORKEY, BUZZER_TIMES_READYFORKEY);

						/**
						 * Wait for the KeyCode
						 * 	Only continue if the current KeyCode is different from the previous MASTER KeyCode. 
						 * 	Then, a MASTER code can't delete itself, and we can prevent situations in which
						 * 	there's no MASTER keys in the KeyStore. 
						 */
						
						// Copy MASTER KeyCode into master_keycode
						uint8_t master_keycode[KEYCODE_KEY_SIZE];
						for(uint8_t i = 0; i < KEYCODE_KEY_SIZE; i++)
							master_keycode[i] = KeyCode[i];

						// Wait until a non-master code is read
						DEBUG_SERIAL_PRINT("\tWaiting for a different code");

						while(1)
						{
							if(readKeyCode())
							{
								if(!keyIsEqual(master_keycode, KeyCode, KEYCODE_KEY_SIZE))
									break;

								DEBUG_SERIAL_PRINT("\t\tThe KeyCode is EQUAL, skipping...");

								// Beep :)
								beep(BUZZER_TIME_WRONGKEY, BUZZER_TIMES_WRONGKEY * 2);
							}
						}

						DEBUG_SERIAL_PRINTA("\t\tKeyCode: 0x", KeyCode, KEYCODE_KEY_SIZE, HEX);

						delay(DELAY_AFTER_MODKEY);

						// If the key is not stored in the memory
						if(!KeyCodeKeyStore.keyIsValid(KeyCode))
						{
							DEBUG_SERIAL_PRINT("\tThe KeyCode is NOT valid, adding...");

							// Add the key
							key_data.master = false;

							// If the key was successfully added
							if(KeyCodeKeyStore.addKey(KeyCode, &key_data))
							{
								DEBUG_SERIAL_PRINT("\t\tok.");

								beep(BUZZER_TIME_OK, BUZZER_TIMES_OK);
							}
							else
							{
								DEBUG_SERIAL_PRINT("\t\terror, the KeyStore may be full.");

								beep(BUZZER_TIME_ERROR, BUZZER_TIMES_ERROR);
							}
						}
						// If IS stored => Remove the key
						else
						{
							DEBUG_SERIAL_PRINT("\tThe KeyCode is valid, removing...");

							KeyCodeKeyStore.removeKey(KeyCode);

							beep(BUZZER_TIME_WRONGKEY, BUZZER_TIMES_WRONGKEY);

							DEBUG_SERIAL_PRINT("\t\tok.");
						}
					}
					// If is not a MASTER coed
					else
					{
						DEBUG_SERIAL_PRINT("\t\tRelay enabled");

						// Enable the relay :P
						enableRelay();

						// Beep :)
						// Shall we?
						beep(BUZZER_TIME_OK, BUZZER_TIMES_OK);
					}
				}
				else
				{
					DEBUG_SERIAL_PRINT("\tThe KeyCode is NOT valid");

					beep(BUZZER_TIME_WRONGKEY, BUZZER_TIMES_WRONGKEY);
				}
			}
		)
	}