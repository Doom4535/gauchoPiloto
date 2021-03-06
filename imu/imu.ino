/*****************************************************************
MPU9150_AHRS_directdata.ino
SFE_MPU9150 Library AHRS Data Fusion Example Code
Kris Winer for Sparkfun Electronics
Original Creation Date: April 8, 2014
https://github.com/sparkfun/MPU9150_Breakout

The MPU9150 is a versatile 9DOF sensor. It has a built-in
accelerometer, gyroscope, and magnetometer that
functions over I2C. It is very similar to the 6 DoF MPU6050 for which an extensive library has already been built.
Most of the function of the MPU9150 can utilize the MPU6050 library.

This Arduino sketch utilizes Jeff Rowberg's MPU6050 library to generate the basic sensor data
for use in two sensor fusion algorithms becoming increasingly popular with DIY quadcopter and robotics engineers. 
I have added and slightly modified Jeff's library here.

This simple sketch will demo the following:
* How to create a MPU6050 object, using a constructor (global variables section).
* How to use the initialize() function of the MPU6050 class.
* How to read the gyroscope, accelerometer, and magnetometer
  using the readAcceleration(), readRotation(), and readMag() functions and the
  gx, gy, gz, ax, ay, az, mx, my, and mz variables.
* How to calculate actual acceleration, rotation speed, magnetic
  field strength using the  specified ranges as described in the data sheet:
  http://dlnmh9ip6v2uc.cloudfront.net/datasheets/Sensors/IMU/PS-MPU-9150A.pdf
  and
  http://dlnmh9ip6v2uc.cloudfront.net/datasheets/Sensors/IMU/RM-MPU-9150A-00.pdf.
  
In addition, the sketch will demo:
* How to check for data updates using the data ready status register
* How to display output at a rate different from the sensor data update and fusion filter update rates
* How to specify the accelerometer and gyro sampling and bandwidth rates
* How to use the data from the MPU9150 to fuse the sensor data into a quaternion representation of the sensor frame
  orientation relative to a fixed Earth frame providing absolute orientation information for subsequent use.
* An example of how to use the quaternion data to generate standard aircraft orientation data in the form of
  Tait-Bryan angles representing the sensor yaw, pitch, and roll angles suitable for any vehicle stablization control application.

Hardware setup: This library supports communicating with the
MPU9150 over I2C. These are the only connections that need to be made:
	MPU9150 --------- Arduino
	 SCL ---------- SCL (A5 on older 'Duinos')
	 SDA ---------- SDA (A4 on older 'Duinos')
	 VDD ------------- 3.3V
	 GND ------------- GND

The LSM9DS0 has a maximum voltage of 3.5V. Make sure you power it
off the 3.3V rail! And either use level shifters between SCL
and SDA or just use a 3.3V Arduino Pro.	  

In addition, this sketch uses a Nokia 5110 48 x 84 pixel display which requires 
digital pins 5 - 9 described below. If using SPI you might need to press one of the A0 - A3 pins
into service as a digital input instead.

Development environment specifics:
	IDE: Arduino 1.0.5
	Hardware Platform: Arduino Pro 3.3V/8MHz
	MPU9150 Breakout Version: 1.0

This code is beerware. If you see me (or any other SparkFun 
employee) at the local, and you've found our code helpful, please 
buy us a round!

Distributed as-is; no warranty is given.
*****************************************************************/
  
#include <Wire.h>
#include "I2Cdev.h"
#include "SoftwareSerial.h"
#include "../autopilot/gps.h"
#include "AHRS.h"
#include "MPU6050_9Axis_MotionApps41.h"


// global constants for 9 DoF fusion and AHRS (Attitude and Heading Reference System)
//#define GyroMeasError PI * (40.0f / 180.0f)       // gyroscope measurement error in rads/s (shown as 3 deg/s)
//#define GyroMeasDrift PI * (0.0f / 180.0f)      // gyroscope measurement drift in rad/s/s (shown as 0.0 deg/s/s)
// There is a tradeoff in the beta parameter between accuracy and response speed.
// In the original Madgwick study, beta of 0.041 (corresponding to GyroMeasError of 2.7 degrees/s) was found to give optimal accuracy.
// However, with this value, the LSM9SD0 response time is about 10 seconds to a stable initial quaternion.
// Subsequent changes also require a longish lag time to a stable output, not fast enough for a quadcopter or robot car!
// By increasing beta (GyroMeasError) by about a factor of fifteen, the response time constant is reduced to ~2 sec
// I haven't noticed any reduction in solution accuracy. This is essentially the I coefficient in a PID control sense; 
// the bigger the feedback coefficient, the faster the solution converges, usually at the expense of accuracy. 
// In any case, this is the free parameter in the Madgwick filtering and fusion scheme.
//#define beta sqrt(3.0f / 4.0f) * GyroMeasError   // compute beta
//#define zeta sqrt(3.0f / 4.0f) * GyroMeasDrift   // compute zeta, the other free parameter in the Madgwick scheme usually set to a small or zero value
//#define Kp 2.0f * 5.0f // these are the free parameters in the Mahony filter and fusion scheme, Kp for proportional feedback, Ki for integral
//#define Ki 0.0f

int16_t a1, a2, a3, g1, g2, g3, m1, m2, m3;     // raw data arrays reading

uint16_t count = 0;  // used to control display output rate
uint16_t delt_t = 0; // used to control display output rate
uint16_t mcount = 0; // used to control display output rate
uint8_t MagRate;     // read rate for magnetometer data

float pitch, yaw, roll;
float deltat = 0.0f;        // integration interval for both filter schemes
uint16_t lastUpdate = 0; // used to calculate integration interval

float ax, ay, az, gx, gy, gz, mx, my, mz; // variables to hold latest sensor data values 
float q[4] = {1.0f, 0.0f, 0.0f, 0.0f};    // vector to hold quaternion
//float eInt[3] = {0.0f, 0.0f, 0.0f};       // vector to hold integral error for Mahony method

Gps gps;
AHRS    ahrs;
SoftwareSerial ss(4, 3);
// Declare device MPU6050 class
MPU6050 mpu;

uint16_t now = 0;        // used to calculate integration interval
void setup()
{
  Serial.begin(38400); // Start serial at 38400 bps
 
           

    // initialize MPU6050 device
    Serial.println(F("Initializing I2C devices..."));
    mpu.initialize();

    // verify connection
    Serial.println(F("Testing device connections..."));
    Serial.println(mpu.testConnection() ? F("MPU9150 connection successful") : F("MPU9150 connection failed"));

// Set up the accelerometer, gyro, and magnetometer for data output

   mpu.setRate(7); // set gyro rate to 8 kHz/(1 * rate) shows 1 kHz, accelerometer ODR is fixed at 1 KHz

   MagRate = 10; // set magnetometer read rate in Hz; 10 to 100 (max) Hz are reasonable values

// Digital low pass filter configuration. 
// It also determines the internal sampling rate used by the device as shown in the table below.
// The accelerometer output rate is fixed at 1kHz. This means that for a Sample
// Rate greater than 1kHz, the same accelerometer sample may be output to the
// FIFO, DMP, and sensor registers more than once.
/*
 *          |   ACCELEROMETER    |           GYROSCOPE
 * DLPF_CFG | Bandwidth | Delay  | Bandwidth | Delay  | Sample Rate
 * ---------+-----------+--------+-----------+--------+-------------
 * 0        | 260Hz     | 0ms    | 256Hz     | 0.98ms | 8kHz
 * 1        | 184Hz     | 2.0ms  | 188Hz     | 1.9ms  | 1kHz
 * 2        | 94Hz      | 3.0ms  | 98Hz      | 2.8ms  | 1kHz
 * 3        | 44Hz      | 4.9ms  | 42Hz      | 4.8ms  | 1kHz
 * 4        | 21Hz      | 8.5ms  | 20Hz      | 8.3ms  | 1kHz
 * 5        | 10Hz      | 13.8ms | 10Hz      | 13.4ms | 1kHz
 * 6        | 5Hz       | 19.0ms | 5Hz       | 18.6ms | 1kHz
 */
  mpu.setDLPFMode(4); // set bandwidth of both gyro and accelerometer to ~20 Hz

// Full-scale range of the gyro sensors:
// 0 = +/- 250 degrees/sec, 1 = +/- 500 degrees/sec, 2 = +/- 1000 degrees/sec, 3 = +/- 2000 degrees/sec
  mpu.setFullScaleGyroRange(0); // set gyro range to 250 degrees/sec

// Full-scale accelerometer range.
// The full-scale range of the accelerometer: 0 = +/- 2g, 1 = +/- 4g, 2 = +/- 8g, 3 = +/- 16g
  mpu.setFullScaleAccelRange(0); // set accelerometer to 2 g range

  mpu.setIntDataReadyEnabled(true); // enable data ready interrupt
}

void loop()
{

  unsigned long age, date, time, chars = 0;
    // read raw accel/gyro measurements from device
  float flat, flon, alt, course, speed_ms;

  /*
  gps.f_get_position(&flat, &flon, &age);
  alt = gps.f_altitude();
  course = gps.f_course();
  */
  speed_ms = gps.get_speed_ms();

  if(mpu.getIntDataReadyStatus() == 1) { // wait for data ready status register to update all data registers
     mcount++;
    // read the raw sensor data
     mpu.getAcceleration  ( &a1, &a2, &a3  );
     ax = a1*2.0f/32768.0f; // 2 g full range for accelerometer
     ay = a2*2.0f/32768.0f;
     az = a3*2.0f/32768.0f;

     mpu.getRotation  ( &g1, &g2, &g3  );
     gx = g1*250.0f/32768.0f; // 250 deg/s full range for gyroscope
     gy = g2*250.0f/32768.0f;
     gz = g3*250.0f/32768.0f;
//  The gyros and accelerometers can in principle be calibrated in addition to any factory calibration but they are generally
//  pretty accurate. You can check the accelerometer by making sure the reading is +1 g in the positive direction for each axis.
//  The gyro should read zero for each axis when the sensor is at rest. Small or zero adjustment should be needed for these sensors.
//  The magnetometer is a different thing. Most magnetometers will be sensitive to circuit currents, computers, and 
//  other both man-made and natural sources of magnetic field. The rough way to calibrate the magnetometer is to record
//  the maximum and minimum readings (generally achieved at the North magnetic direction). The average of the sum divided by two
//  should provide a pretty good calibration offset. Don't forget that for the MPU9150, the magnetometer x- and y-axes are switched 
//  compared to the gyro and accelerometer!
     if (mcount > 1000/MagRate) {  // this is a poor man's way of setting the magnetometer read rate (see below) 
       mpu.getMag  ( &m1, &m2, &m3 );
       mx = m1*10.0f*1229.0f/4096.0f + 18.0f; // milliGauss (1229 microTesla per 2^12 bits, 10 mG per microTesla)
       my = m2*10.0f*1229.0f/4096.0f + 70.0f; // apply calibration offsets in mG that correspond to your environment and magnetometer
       mz = m3*10.0f*1229.0f/4096.0f + 270.0f;
       mcount = 0;
     }           
  }
   
  now = micros();
  deltat = ((now - lastUpdate)/1000000.0f); // set integration time by time elapsed since last filter update
  lastUpdate = now;
  // Sensors x (y)-axis of the accelerometer is aligned with the y (x)-axis of the magnetometer;
  // the magnetometer z-axis (+ down) is opposite to z-axis (+ up) of accelerometer and gyro!
  // We have to make some allowance for this orientationmismatch in feeding the output to the quaternion filter.
  // For the MPU-9150, we have chosen a magnetic rotation that keeps the sensor forward along the x-axis just like
  // in the LSM9DS0 sensor. This rotation can be modified to allow any convenient orientation convention.
  // This is ok by aircraft orientation standards!  
  // Pass gyro rate as rad/s
   ahrs.MadgwickQuaternionUpdate(ax, ay, az, gx*PI/180.0f, gy*PI/180.0f, gz*PI/180.0f,  my,  mx, mz);
// ahrs.MahonyQuaternionUpdate(ax, ay, az, gx*PI/180.0f, gy*PI/180.0f, gz*PI/180.0f, my, mx, mz);

    // Serial print and/or display at 0.5 s rate independent of data rates
  delt_t = millis() - count;
  if (delt_t > 500) { // update LCD once per half-second independent of read rate

    Serial.print("ax = "); Serial.print((int)1000*ax);  
    Serial.print(" ay = "); Serial.print((int)1000*ay); 
    Serial.print(" az = "); Serial.print((int)1000*az); Serial.println(" mg");
    Serial.print("gx = "); Serial.print( gx, 2); 
    Serial.print(" gy = "); Serial.print( gy, 2); 
    Serial.print(" gz = "); Serial.print( gz, 2); Serial.println(" deg/s");
    Serial.print("mx = "); Serial.print( (int)mx ); 
    Serial.print(" my = "); Serial.print( (int)my ); 
    Serial.print(" mz = "); Serial.print( (int)mz ); Serial.println(" mG");
    
    Serial.print("q0 = "); Serial.print(q[0]);
    Serial.print(" qx = "); Serial.print(q[1]); 
    Serial.print(" qy = "); Serial.print(q[2]); 
    Serial.print(" qz = "); Serial.println(q[3]); 
                   
    
  // Define output variables from updated quaternion---these are Tait-Bryan angles, commonly used in aircraft orientation.
  // In this coordinate system, the positive z-axis is down toward Earth. 
  // Yaw is the angle between Sensor x-axis and Earth magnetic North (or true North if corrected for local declination, looking down on the sensor positive yaw is counterclockwise.
  // Pitch is angle between sensor x-axis and Earth ground plane, toward the Earth is positive, up toward the sky is negative.
  // Roll is angle between sensor y-axis and Earth ground plane, y-axis up is positive roll.
  // These arise from the definition of the homogeneous rotation matrix constructed from quaternions.
  // Tait-Bryan angles as well as Euler angles are non-commutative; that is, the get the correct orientation the rotations must be
  // applied in the correct order which for this configuration is yaw, pitch, and then roll.
  // For more see http://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles which has additional links.
    yaw   = atan2(2.0f * (q[1] * q[2] + q[0] * q[3]), q[0] * q[0] + q[1] * q[1] - q[2] * q[2] - q[3] * q[3]);   
    pitch = -asin(2.0f * (q[1] * q[3] - q[0] * q[2]));
    roll  = atan2(2.0f * (q[0] * q[1] + q[2] * q[3]), q[0] * q[0] - q[1] * q[1] - q[2] * q[2] + q[3] * q[3]);
    pitch *= 180.0f / PI;
    yaw   *= 180.0f / PI - 13.8; // Declination at Danville, California is 13 degrees 48 minutes and 47 seconds on 2014-04-04
    roll  *= 180.0f / PI;

    Serial.print("Yaw, Pitch, Roll: ");
    Serial.print(yaw, 2);
    Serial.print(", ");
    Serial.print(pitch, 2);
    Serial.print(", ");
    Serial.println(roll, 2);
    
    Serial.print("rate = "); Serial.print((float)1.0f/deltat, 2); Serial.println(" Hz");


    Serial.print (flat,2);
    
    Serial.print (flon, 2);
    Serial.print (alt, 2);
    Serial.print (course,2);
    Serial.print (speed,2);
 
    // With these settings the filter is updating at a ~145 Hz rate using the Madgwick scheme and 
    // >200 Hz using the Mahony scheme even though the display refreshes at only 2 Hz.
    // The filter update rate is determined mostly by the mathematical steps in the respective algorithms, 
    // the processor speed (8 MHz for the 3.3V Pro Mini), and the magnetometer ODR:
    // an ODR of 10 Hz for the magnetometer produce the above rates, maximum magnetometer ODR of 100 Hz produces
    // filter update rates of 36 - 145 and ~38 Hz for the Madgwick and Mahony schemes, respectively. 
    // This is presumably because the magnetometer read takes longer than the gyro or accelerometer reads.
    // This filter update rate should be fast enough to maintain accurate platform orientation for 
    // stabilization control of a fast-moving robot or quadcopter. Compare to the update rate of 200 Hz
    // produced by the on-board Digital Motion Processor of Invensense's MPU6050 6 DoF and MPU9150 9DoF sensors.
    // The 3.3 V 8 MHz Pro Mini is doing pretty well!
    count = millis();
  }
}



